#ifndef PROCESSOR_HPP
#define PROCESSOR_HPP

#include <cstdint>

#define DEFAULT_K0 1
#define DEFAULT_K1 2
#define DEFAULT_K2 3
#define DEFAULT_R 2
#define DEFAULT_F 4

typedef struct _processor_inst_t
{
    uint32_t instruction_address;
    int32_t op_code;
    int32_t src_reg[2];
    int32_t dest_reg;
    uint32_t branch_target;     // Branch target. Changed from the template
    int32_t take;               // Actual behavior of the branch.
    
} processor_inst_t;

typedef struct _processor_stats_t
{
    unsigned long cycle_count;
    float avg_d_queue;
    float avg_ipc;
    float avg_inst_fire;
    float avg_inst_retired;
    float perc_branch_pred;
    unsigned long retired_instruction;  // retired instruction count
    unsigned long max_d_queue;          // max dispatch queue size
    unsigned long b_inst_count;         // branch instruction count
    unsigned long b_inst_correct;       // correct branch instruction count
    unsigned long inst_issued;          // number of instructions issued to reservation station
    unsigned long inst_fired;           // number of instructions executed

} processor_stats_t;

typedef struct _b_info                  // branch info container
{
    char b_index = 0;                   // Branch predictor index value, containing branch history for that instruction
                                        // and hashed address value.
    uint32_t branch_target = 0;         // branch target
    int32_t actual_result = 0;          // branch actual behavior
    
} b_info;

typedef struct _rstation                // reservation station implementation
{
    bool busy;                          // indicate whether the station is busy
    bool executing;                     // indicate whether the instruction for that reservation station is currently executing
    uint64_t tag_number;                // number of instruction
    int32_t opcode;
    int32_t dest;
    int32_t q1;
    int32_t q2;
    int32_t v1;
    int32_t v2;
    b_info branch_info;                 // branch info object for branch instructions

} rstation;

typedef struct _location                // location of reservation stations to trace the reservation station of the inst
{
    int32_t fu_type;
    uint64_t funit;
    uint64_t rs_no;

} location;

typedef struct _reg                     // register implementation. Since no actual values are calculated,
{                                       // simply indicate if an instruction is working on it or not.
                                        // WAW hazard is handled in cdb, not through register table just like Tomasulo.
    uint64_t tag_number;
    bool busy;
    
} reg;

typedef struct _f_units                 // Individual function unit implementation
{
    uint64_t max_inst;
    rstation * rstations;               // function unit has r number of reservation stations
    
} f_units;

typedef struct _f_unit_type             // Individual function unit type implementation
{
    f_units *func_units;                // each function type has ki number of function units
    uint64_t no_fu;                     // number of function units for given function type
    int latency;                        // latency for the function type
    int type;                           // Numerical indicator for function unit type
    
} f_unit_type;

typedef struct _tag                     // Implementation of tag object that stores the trace of each instruction
{
    //Use brace-or-equal initializers, added in C++11
    uint64_t tag_number = 0;            // The instruction number starts from 0 FYI as per the final exam specification
    uint64_t fetch = 0;
    uint64_t dispatch = 0;
    uint64_t schedule = 0;
    uint64_t execute = 0;
    uint64_t update = 0;
} tag;

typedef struct _inst_tag                // Wrapper for instruction, adding tag information and predictor table index,
{
    processor_inst_t inst;
    uint64_t tag_number;
    char b_index = 0;                   // Predictor table index, containing branch history and hashed address value
    
} inst_tag;

typedef struct _exec_inst               // Object to communicate between execute, schedule, and update stage
{
    uint64_t tag_number;
    uint32_t branch_target = 0;
    int cycles_remaining;
    int32_t dest;
    location rs_loc;                    // reservation station location object
    int32_t actual_result = 0;          // Actual branch behavior
    char b_index = 0;                   // branch predictor index
    
} exec_inst;


bool read_instructs(processor_inst_t* p_inst);
void fetch(processor_stats_t* p_stats);
void dispatch(processor_stats_t* p_stats);
void schedule(processor_stats_t* p_stats);
void execute(processor_stats_t* p_stats);
void update(processor_stats_t* p_stats);
bool insert_to_rs(inst_tag *tagged_inst);
int empty_fu(int fu_type);

void setup_processor(uint64_t r, uint64_t k0, uint64_t k1, uint64_t k2, uint64_t f);
void run_processor(processor_stats_t* p_stats);
void complete_processor(processor_stats_t* p_stats);

#endif /* PROCESSOR_HPP */
