#include <cstdio>
#include <cinttypes>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include "processor.hpp"
#include <string>

FILE* inFile = stdin;

void print_help_and_exit(void) {
    printf("procsim [OPTIONS]\n");
    printf("  -k k0\t\tNumber of k0 FUs\n");
    printf("  -l k1\t\tNumber of k1 FUs\n");
    printf("  -m k2\t\tNumber of k2 FUs\n");
    printf("  -f \t\tNumber of instructions to fetch\n");
    printf("  -r \t\tNumber of reservation stations \n");
    printf("  -i traces/file.trace\n");
    printf("  -h\t\tThis helpful output\n");
    exit(0);
}

//
// read_instructs
//
//  returns true if an instruction was read successfully
//
bool read_instructs(processor_inst_t* p_inst)
{
    int ret;
    
    if (p_inst == NULL)
    {
        fprintf(stderr, "Fetch requires a valid pointer to populate\n");
        return false;
    }
    
    // ret = fscanf(stdin, "%x %d %d %d %d\n", &p_inst->instruction_address,
    //              &p_inst->op_code, &p_inst->dest_reg, &p_inst->src_reg[0], &p_inst->src_reg[1]); 

    // Modified fscanf to sscanf to accommodate branch target and behavior at the tail of the instruction.
    char line[256];
    fgets(line, 256, stdin);

    p_inst->branch_target = 0;
    p_inst->take = 0;

    ret = sscanf(line, "%x %d %d %d %d %x %d\n", &p_inst->instruction_address,
                  &p_inst->op_code, &p_inst->dest_reg, &p_inst->src_reg[0], &p_inst->src_reg[1], &p_inst->branch_target, &p_inst->take);

    if (ret != 5 && ret != 7) {
        return false;
    }
    
    return true;
}


void print_statistics(processor_stats_t* p_stats);

int main(int argc, char* argv[]) {
    int opt;
    uint64_t f = DEFAULT_F;
    uint64_t k0 = DEFAULT_K0;
    uint64_t k1 = DEFAULT_K1;
    uint64_t k2 = DEFAULT_K2;
    uint64_t r = DEFAULT_R;

    /* Read arguments */ 
    while(-1 != (opt = getopt(argc, argv, "r:i:k:l:m:f:h"))) {
        switch(opt) {
        case 'r':
            r = atoi(optarg);
            break;
        case 'k':
            k0 = atoi(optarg);
            break;
        case 'l':
            k1 = atoi(optarg);
            break;
        case 'm':
            k2 = atoi(optarg);
            break;
        case 'f':
            f = atoi(optarg);
            break;
        case 'i':
            inFile = fopen(optarg, "r");
            if (inFile == NULL)
            {
                fprintf(stderr, "Failed to open %s for reading\n", optarg);
                print_help_and_exit();
            }
            break;
        case 'h':
            /* Fall through */
        default:
            print_help_and_exit();
            break;
        }
    }

    printf("Processor Settings\n");
    printf("R: %" PRIu64 "\n", r);
    printf("k0: %" PRIu64 "\n", k0);
    printf("k1: %" PRIu64 "\n", k1);
    printf("k2: %" PRIu64 "\n", k2);
    printf("F: %"  PRIu64 "\n", f);
    printf("\n");

    /* Setup the processor */
    setup_processor(r, k0, k1, k2, f);

    /* Setup statistics */
    processor_stats_t stats;
    memset(&stats, 0, sizeof(processor_stats_t));

    /* Run the processor */
    run_processor(&stats);

    /* Finalize stats */
    complete_processor(&stats);

    print_statistics(&stats);

    return 0;
}

void print_statistics(processor_stats_t* p_stats) {
    printf("Processor stats:\n");
	printf("Total instructions: %lu\n", p_stats->retired_instruction);
	printf("Total run time (cycles): %lu\n", p_stats->cycle_count);

	printf("Average Dispatch queue: %f\n", p_stats->avg_d_queue);
	printf("Maximum Dispatch queue: %lu\n", p_stats->max_d_queue);

	printf("Avg inst issued per cycle: %f\n", p_stats->avg_ipc);
	printf("Avg inst fired per cycle: %f\n", p_stats->avg_inst_fire);
	printf("Avg inst retired per cycle: %f\n", p_stats->avg_inst_retired);

	printf("Total branch instructions: %lu\n", p_stats->b_inst_count);
	printf("Total correct predicted branch instructions: %lu\n", p_stats->b_inst_correct);
	printf("Branch Prediction Accuracy: %f\n", p_stats->perc_branch_pred);
}

