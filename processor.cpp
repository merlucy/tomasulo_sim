#include "processor.hpp"
#include <cstdio>
#include <cstring>
#include <queue>
#include <map>

f_unit_type funits[3];                      // An array of function unit types. Hard coded the number of types of FUs as 3.
reg registers[32];                          // Registers Dashboard. It is an array of reg (shorthand for register).

f_unit_type k0_fu;                          // k0 function unit object
f_unit_type k1_fu;                          // k1 function unit object
f_unit_type k2_fu;                          // k2 function unit object

uint64_t inst_counter = 0;                  // Instruction Counter incremented as new instructions are fetched.
                                            // First instruction is tagged as 0, following the final exam requirement.
uint64_t cycle_counter = 0;                 // Counter to record current cycle
uint64_t i_per_cycle;                       // Instructions per cycle
uint64_t max_inst = 0;                      // Max inst per function unit within each function type. Equivalent to user input "r".

std::queue<inst_tag> d_queue;               // Declaration of Dispatch Queue
std::queue<exec_inst> sch_q;                // Declaration of Scheduler Queue
std::map<uint64_t, tag> tag_map;            // Map to store tag data <Key: inst number, Value: tag object (stage execution cycle info)

std::map<uint64_t, exec_inst> update_queue; // Update Queue. Uses map since elements are ordered by the key value, which in this case is
                                            // the tag number (instruction number). This is to update the reservation stations in the
                                            // order of tag number. (Note: Bus Arbitration, page 7 of Exam Paper)

std::vector<exec_inst> cdb_lookup[32];      // An array of Vectors to lookup history of instructions written to cdb.
                                            // A measure to handle WAW data hazard. Since multiple instructions can write to the same 
                                            // register (WAW data hazard), instead of explicit renaming of registers, this allows operand 
                                            // dependent instructions to look up correct instruction has been updated.

char branch_predictor[128];                 // 3 bit GHR + 4 lower bits of branch target address = 128 entries.
                                            // Each entry holds up to 2 bits (bimodal).

char branch_history = 0;                    // Branch History will be managed to store only 3 bits (3 previous branches) based on the
                                            // specification in the exam paper for Global History Register to be 3 bit wide.

std::queue<processor_inst_t> delayed_queue; // There are two cases when delayed_queue is populated. First, when predict-taken but to store
                                            // delayed instructions if the branch was mispredicted. Second, when predict not-taken and
                                            // not-taking the branch results in going past the target branch, we store the instructions from
                                            // that target branch position.
                                            // Update: Initially, the above functions were implemented by storing actual intructions that 
                                            // meets the criteria above, I have found that this function is not useful since all the trace
                                            // files have the target branch very next to the branch instruction. As a result, the delayed queue
                                            // now acts as a way to stall the fetch unit.

bool branch_mispredicted = false;           // If the branch is mispredicted, stall the fetch unit and dispatch unit as the appropriate
                                            // instructions are inserted.

float avg_d_queue = 0;                      // Average d_queue is accumulated by multiplying 
                                            // (previous avg_d_queue*(cycle_counter prior to current) + (current d_queue size)) / (current cycle)

/**
 * initializing the processor. 
 *
 * @k0 Number of k0 FUs
 * @k1 Number of k1 FUs
 * @k2 Number of k2 FUs
 * @f Number of instructions to fetch
 * @r reservation units
 */
void setup_processor(uint64_t r, uint64_t k0, uint64_t k1, uint64_t k2, uint64_t f) {

    // Initialize function units container
    k0_fu.type = 0;
    k0_fu.latency = 1;  // Latency 1 for FU type 1
    k0_fu.no_fu = k0;
    k0_fu.func_units = new f_units[k0];

    k1_fu.type = 1;
    k1_fu.latency = 3;  // Latency 3 for FU type 2
    k1_fu.no_fu = k1;
    k1_fu.func_units = new f_units[k1];

    k2_fu.type = 2;
    k2_fu.latency = 10; // Latency 10 for FU type 3
    k2_fu.no_fu = k2;
    k2_fu.func_units = new f_units[k2];

    funits[0] = k0_fu;
    funits[1] = k1_fu;
    funits[2] = k2_fu;

    for(int i = 0; i < 3; i++){

        for(int j = 0; j < funits[i].no_fu; j++){
            funits[i].func_units[j].rstations = new rstation[r];
            funits[i].func_units[j].max_inst = r;

            for(int k = 0; k < r; k++){

                // -2 to identify it as empty
                funits[i].func_units[j].rstations[k].opcode = -2;   // if opcode is -2, the reservation station is empty.
                funits[i].func_units[j].rstations[k].tag_number = 0;
                funits[i].func_units[j].rstations[k].busy = false;
                funits[i].func_units[j].rstations[k].executing = false;
                funits[i].func_units[j].rstations[k].dest = -2;
                funits[i].func_units[j].rstations[k].q1 = -2;
                funits[i].func_units[j].rstations[k].q2 = -2;
                funits[i].func_units[j].rstations[k].v1 = -2;
                funits[i].func_units[j].rstations[k].v2 = -2;

                b_info branch_info = b_info();
                funits[i].func_units[j].rstations[k].branch_info = branch_info; // branch_info will be used to store branch data
                                                                                // for branch instructions.
            }
        }
    }
    
    i_per_cycle = f;        // Initialize i_per_cycle (instructions fetched per cycle)
    max_inst = r;           // Set user input of r as max_inst per function unit (within function unit type)

    // initialize result registers
    for(int i = 0; i < 32; i++){

        // registers[i].busy to indicate if any instruction is writing to this register right now.
        // WAW hazard is more robustly handled by sending results through cdb directly, so the registers 
        // are used to check for "potential" dependencies in a quick manner.
        registers[i].tag_number = (uint64_t) 0;
        registers[i].busy = false;
    }

    // initialize branch predictor table with initial state "01"
    for(int j = 0; j < 128; j++){
        branch_predictor[j] = 1;    // Set to state "01"
    }
}

void fetch(processor_stats_t* p_stats){

    // Fetch new instructions into the dispatch queue - 1 cycle
    for(int i = 0; i < i_per_cycle; i++){
        processor_inst_t new_inst;

        // If branch was mispredicted, stall the fetching, and get the correct instructions
        if(branch_mispredicted){

            if(!delayed_queue.empty()){
                new_inst = delayed_queue.front();
                delayed_queue.pop();
            }else{
                // If all instructions inside the delayed_queue is fetched, break the loop.
                // This effectively gives one cycle of stall.
                branch_mispredicted = false;
                break;
            }

        }else{
            new_inst = processor_inst_t();
            bool read_results = read_instructs(&new_inst);

            // If no more instructions to fetch, return.
            if(!read_results){
                return;
            }
        }

        // If the op_code is -1, we check if this is a valid branching instruction.
        // Check for instructions in the form of (addr -1 -1 -1 -1 VS addr -1 -1 -1 -1 addr behavior)
        if(new_inst.op_code == -1){

            // If the branching instruction is a valid one, check with the predictor to decide whether to take or not take 
            if(new_inst.branch_target != 0){

                // Create new tags for the newly introduced instruction.
                // Every instruction has a tag even if the instruction is skipped due to branching.
                tag new_tag = tag();                            // Create a new tag to insert into tag_map
                new_tag.tag_number = inst_counter;              // Set the tag number as the current instruction count
                new_tag.fetch = cycle_counter;                  // Record the cycle at which the instruction was fetched.

                inst_tag tagged_inst;                           // Wrap an instruction with the tag number using inst_tag struct.
                tagged_inst.inst = new_inst;                    // Contain the instruction object.

                // Calculate the branch index lookup within the predictor table.
                // Calculate the hashed instruction address and append it to the current branch history.
                char target_hashed = (new_inst.instruction_address >> 2) % 16;
                char b_index = target_hashed + (branch_history << 4);   // Since GHR is 3 bits wide, shift left 4 bits

                // If the predictor indicates as TAKE (11 or 10) and if not currently branching, branch to the target
                if(((branch_predictor[b_index] & 2) == 2)){
                    tagged_inst.b_index = b_index;
                
                // Else, does not take the branch
                }else{
                    tagged_inst.b_index = b_index;
                }

                // Modify the instruction object in order for the latter stages understand the latency and
                // the type of this instruction. dest_reg of -2 will be checked in the update stage, and op_code of 0
                // will have a latency of 1.
                tagged_inst.inst.op_code = 0;
                tagged_inst.inst.dest_reg = -2;                 // destination register as -2 to indicate that this is a branch instruction
                                                                // actual behavior. (Tagged with the instruction just for communication)
                tagged_inst.tag_number = new_tag.tag_number;    // Set the tag number for the given instruction. The reason we don't copy the tag 
                                                                // is that we will record the tag stage cycle by referring to tag_map. 
                                                                // If the tag never executes due to branching, the instruction is not recorded in the tag_map!!

                // Push the instruction to dispatch queue and insert the tag into the tag_map
                d_queue.push(tagged_inst);
                tag_map.insert({inst_counter, new_tag});

                inst_counter += 1;  // Increment the instruction counter

                continue;       // Skip to the next iteration of fetching. Fetching is not stalled.

            // If the branch instruction with op_code of -1 is not a valid branch operation with not branch target,
            // set the op_code as 1.
            }else if(new_inst.branch_target == 0){
                new_inst.op_code = 1;   // Update the instruction's op_code as 1.
                                        // FU type 1 will have a latency of 3.
            }
        }

        // // If the program is branching, put the instruction inside the delayed instruction queue. We might have to come back to this when
        // // we find out the branch is mispredicted.
        // if(is_branching){
        //     delayed_queue.push(new_inst);               // Push the delayed instructions before the target branch to delayed queue
        //                                                 // for execution in case mispredicted. The tag is not inserted into the tag_map
        //                                                 // because we are not sure if the instruction will complete the processor cycles.

        // Need to set inst_counter to the inst_counter of tagged_inst variable
        // Need to insert into tag_map
    
        // Create new tags for the newly introduced instruction.
        // Every instruction has a tag even if the instruction is skipped due to branching.
        tag new_tag = tag();                            // Create a new tag to insert into tag_map
        new_tag.tag_number = inst_counter;              // Set the tag number as the current instruction count
        new_tag.fetch = cycle_counter;                  // Record the cycle at which the instruction was fetched.

        inst_tag tagged_inst;                           // Wrap an instruction with the tag number using inst_tag struct.
        tagged_inst.inst = new_inst;                    // Contain the instruction object.

        tagged_inst.tag_number = new_tag.tag_number;    // Set the tag number for the given instruction. The reason we don't copy the tag 
                                                        // is that we will record the tag stage cycle by referring to tag_map. 
                                                        // If the tag never executes due to branching, the instruction is not recorded in the tag_map!!

        d_queue.push(tagged_inst);                      // push the instruction to the dispatch queue. Actual cycle will be recorded in the next
                                                        // dispatch stage so that the instruction stays at least one cycle inside fetch.
        tag_map.insert({tagged_inst.tag_number, new_tag});        // Insert the new tag into the tag_map

        inst_counter += 1;                              // Increment the instruction counter
    }
}

// Returns the index of the empty func unit within the func type
int empty_fu(int32_t fu_type){

    uint64_t funit_count = funits[fu_type].no_fu;
    
    for(int i = 0; i < funit_count; i++){
        for(int j = 0; j < max_inst; j++){

            if(funits[fu_type].func_units[i].rstations[j].opcode == -2){
                return i;
            }
        }
    }
    return -1;
}

// Insert to the reservation station only if the function unit is not empty
bool insert_to_rs(inst_tag *tagged_inst, processor_stats_t *p_stats){

    // If the function units within a function type have a room, return the function unit index (not function type no.)
    int emp_fu = empty_fu(tagged_inst->inst.op_code);
    
    // If there is a room inside the function unit reservation stations, insert it to the reservation station.
    if(emp_fu != -1){
        
        f_units *t = &funits[tagged_inst->inst.op_code].func_units[emp_fu];

        // For the given empty function unit indicated by emp_fu, find the exact reservation station that is empty
        for(int i = 0; i < t->max_inst; i++){

            if(t->rstations[i].opcode == -2){       // If the reservation station is empty

                t->rstations[i].opcode = tagged_inst->inst.op_code;
                t->rstations[i].busy = true;                            // Set the reservation state to busy
                t->rstations[i].tag_number = tagged_inst->tag_number;
                t->rstations[i].dest = tagged_inst->inst.dest_reg;

                int32_t source_1 = tagged_inst->inst.src_reg[0];
                int32_t source_2 = tagged_inst->inst.src_reg[1];

                // If the register is not needed, set q1 to -1
                if(source_1 == -1){
                    t->rstations[i].q1 = -1;

                // If the register is ready with a value, set q1 to 0
                // This confirms that the source RS has been updated by previous instructions, and the register contains a value
                }else if(registers[source_1].busy == false){
                    t->rstations[i].q1 = 0;

                // If the register is not ready and is busy, set q1 to 1
                }else{
                    t->rstations[i].q1 = 1;
                }

                // If the register is not needed, set q2 to -1
                if(source_2 == -1){
                    t->rstations[i].q2 = -1;

                // If the register is ready with a value, set q2 to 0
                // This confirms that the source RS has been updated by previous instructions, and the register contains a value
                }else if(registers[source_2].busy == false){
                    t->rstations[i].q2 = 0;

                // If the register is not ready and is busy, set q2 to 1
                }else{
                    t->rstations[i].q2 = 1;
                }

                // Record the source register location in v1 and v2
                t->rstations[i].v1 = tagged_inst->inst.src_reg[0];
                t->rstations[i].v2 = tagged_inst->inst.src_reg[1];

                // If the instruction is a branch instruction, populate the branch info for the tagged instruction
                if(tagged_inst->inst.dest_reg == -2){
                    t->rstations[i].branch_info.actual_result = tagged_inst->inst.take;
                    t->rstations[i].branch_info.branch_target = tagged_inst->inst.branch_target;
                    t->rstations[i].branch_info.b_index = tagged_inst->b_index;
                }
                
                // printf("Input result %d ", funits[tagged_inst->inst.op_code].func_units[emp_fu].rstations[i].tag_number);
                // printf(" %d ", funits[tagged_inst->inst.op_code].func_units[emp_fu].rstations[i].opcode);
                // printf(" %d ", funits[tagged_inst->inst.op_code].func_units[emp_fu].rstations[i].dest);
                // printf(" %d ", funits[tagged_inst->inst.op_code].func_units[emp_fu].rstations[i].q1);
                // printf(" %d ", funits[tagged_inst->inst.op_code].func_units[emp_fu].rstations[i].q2);
                // printf(" %d ", funits[tagged_inst->inst.op_code].func_units[emp_fu].rstations[i].v1);
                // printf(" %d \n", funits[tagged_inst->inst.op_code].func_units[emp_fu].rstations[i].v2);

                // If dest_reg is neither -1 (no register target) or -2 (branch instruction)
                if(tagged_inst->inst.dest_reg >= 0){

                    // Update Result Registers
                    registers[tagged_inst->inst.dest_reg].tag_number = tagged_inst->tag_number;
                    registers[tagged_inst->inst.dest_reg].busy = true;
                }
                
                // Record the timing of the instruction going into the scheduler.
                // +1 since the instruction is effectively inside the scheduler next cycle.
                tag_map[tagged_inst->tag_number].schedule = cycle_counter + 1;
                p_stats->inst_issued += 1;

                return true;    // return true if inserted
            }
        }
    }
    return false;   // return false if not inserted
}

void dispatch(processor_stats_t* p_stats){
    std::queue<inst_tag> temp_queue;    // New queue used to store instructions not inserted into the scheduler
                                        // due to resource conflicts (vacancy of function units).

    if(p_stats->max_d_queue < d_queue.size()){
        p_stats->max_d_queue = d_queue.size();
        avg_d_queue = (avg_d_queue * cycle_counter + d_queue.size()) / (cycle_counter + 1);
    }

    // Perform dispatching of instructions to the reservation stations
    while(!d_queue.empty()){

        inst_tag tagged_inst = d_queue.front();
        d_queue.pop();

        // Record the timing of the instruction entering the dispatch queue
        if(tag_map[tagged_inst.tag_number].dispatch == 0){
            tag_map[tagged_inst.tag_number].dispatch = cycle_counter;
        }

        bool insert_result = insert_to_rs(&tagged_inst, p_stats); // Attempt to insert to the reservation station

        if(!insert_result){
            // Insert back into the new queue to make the instruction to go through the next dispatch cycle
            temp_queue.push(tagged_inst);
        }
    }

    d_queue = temp_queue;   // Free and replace the old d_queue
}

void schedule(processor_stats_t* p_stats){

    // Check for every reservation station to see if there are new instructions ready to be executed
    for(int i = 0; i < 3; i++){
        for(int j = 0; j < funits[i].no_fu; j++){
            for(int k = 0; k < funits[i].func_units[j].max_inst; k++){

                rstation * rs = &funits[i].func_units[j].rstations[k];

                // For non-empty reservation station that and not executing right now.
                if(rs->opcode != -2 && !rs->executing){

                    // Check for operands from the cdb lookup
                    // If q1 was not ready previously, check if it is ready now.
                    if(rs->q1 == 1){
                        if(registers[rs->v1].tag_number == 0){
                            rs->q1 = 0;     // If the register is ready

                        }else{

                            // This part is used to fetch the correct value from multiple instructions that have the same destination
                            // register. This part checks for WAW. However, since the current trace file does not have actual values for
                            // computation, this is an abstract checking of data hazards and dependencies.
                            // The inner working of this is that, this checks for results written by instructions issued before the
                            // subject instruction looking for value. This is because instructions issued after the instruction looking for
                            // a value is not important.
                            for(std::vector<exec_inst>::iterator itr = cdb_lookup[rs->v1].begin(); itr != cdb_lookup[rs->v1].end(); itr++){
                                if(itr[0].tag_number < rs->tag_number){
                                    rs->q1 = 0; // Update the status of the readiness of the source
                                }
                            }
                        }
                    }

                    // If q2 was not ready previously, check if it is ready now.
                    if(rs->q2 == 1){
                        if(registers[rs->v2].tag_number == 0){
                            rs->q2 = 0;

                        }else{

                            // This part is used to fetch the correct value from multiple instructions that have the same destination
                            // register. This part checks for WAW. However, since the current trace file does not have actual values for
                            // computation, this is an abstract checking of data hazards and dependencies.
                            // The inner working of this is that, this checks for results written by instructions issued before the
                            // subject instruction looking for value. This is because instructions issued after the instruction looking for
                            // a value is not important.
                            for(std::vector<exec_inst>::iterator itr = cdb_lookup[rs->v2].begin(); itr != cdb_lookup[rs->v2].end(); itr++){
                                if(itr[0].tag_number < rs->tag_number){
                                    rs->q2 = 0;
                                }
                            }
                        }
                    }

                    // If both q1 and q2 ready, add to schedule queue.
                    // q code of 1 = busy; q code of 0 = free register or completed register; q code of -1 = no register;          
                    if(rs->q1 < 1 && rs->q2 < 1){

                        // Create reservation station info container reg 
                        location rs_loc;
                        rs_loc.fu_type = i;
                        rs_loc.funit = j;
                        rs_loc.rs_no = k;

                        // Create wrapper for inst info and rs location
                        exec_inst new_exec_inst;
                        new_exec_inst.tag_number = rs->tag_number;
                        new_exec_inst.dest = rs->dest;
                        new_exec_inst.rs_loc = rs_loc;
                        new_exec_inst.cycles_remaining = funits[i].latency;
                        new_exec_inst.actual_result = rs->branch_info.actual_result;
                        new_exec_inst.b_index = rs->branch_info.b_index;
                        new_exec_inst.branch_target = rs->branch_info.branch_target;

                        // Push to schedule queue
                        sch_q.push(new_exec_inst);
                        rs->executing = true;

                        // Update tag map for the timing of entering the schedule queue
                        // cycle_counter + 1 represents the timing the instruction enters the execute stage
                        tag_map[new_exec_inst.tag_number].execute = cycle_counter + 1;
                        p_stats->inst_fired += 1;
                        
                    }else{
                        // skip, wait for results
                    }
                }
            }
        }
    }
}

void execute(processor_stats_t* p_stats){
    
    // New queue to store the non-finished instructions
    std::queue<exec_inst> temp_queue = std::queue<exec_inst>();

    // While the schedule queue is not empty
    while(!sch_q.empty()){
        exec_inst e_inst = sch_q.front();
        sch_q.pop();

        // If the instruction has stayed in the scheduler queue for the required latnecy, 
        // Add to update queue.
        if(e_inst.cycles_remaining == 0){
            update_queue.insert({ e_inst.tag_number, e_inst });

        }else{
            // Decrement the cycle remaining for instructions that are not completed
            e_inst.cycles_remaining -= 1;
            temp_queue.push(e_inst);
        }
    }

    sch_q = temp_queue; // Replace the old queue
}

void update(processor_stats_t* p_stats){

    // Clear the cdb prior to adding new results for update
    for(int i = 0; i < 32; i++){
        cdb_lookup[i].clear();
    }

    // If the update queue is not empty, iterate through the update queue and update cdb accordingly
    if(!update_queue.empty()){

        // The update queue is iterated in order of tag_number. Thus, it maintains the order of execution, 
        // resulting in only one machine state
        for(std::map<uint64_t, exec_inst>::iterator i = update_queue.begin(); i != update_queue.end(); i++){

            exec_inst updated_inst = i->second;

            tag_map[updated_inst.tag_number].update = cycle_counter;
            // If the instruction's destination is -2, it is a branch instruction. 
            // Result of predictor value compared with actual behavior is compared.
            if(updated_inst.dest == -2){

                p_stats->b_inst_count += 1;

                char actual_behavior = updated_inst.actual_result*2;
                char prediction = (branch_predictor[updated_inst.b_index] & 2);

                if(prediction == actual_behavior){
                    p_stats->b_inst_correct += 1;

                    // Flush the delayed queue. In the case of predicting to take a branch, delayed queue stores the instructions
                    // before the branch target. In the case of predicting to not take a branch, then delayed queue stores the
                    // instructions "from" the branch target if actual behavior calculation takes long enough for the fetch step to
                    // go past the branch target.
                    std::queue<processor_inst_t> empty;
                    std::swap(delayed_queue, empty);

                }else{
                    // Broadcast to roll back to not-taken instructions or move to branch target position
                    branch_mispredicted = true;
                }

                // Previous branch predictor 2-bit value
                char prev = branch_predictor[updated_inst.b_index];

                // If the previous 2-bit value was either 11 or 10
                if((prev & 2) == 2){
                    if(updated_inst.actual_result == 1){
                        branch_predictor[updated_inst.b_index] = (prev | 1);
                    }else{
                        branch_predictor[updated_inst.b_index] = (prev << 1) & 2;
                    }

                // If the previous 2-bit value was either 00 or 01
                }else{
                    if(updated_inst.actual_result == 1){
                        branch_predictor[updated_inst.b_index] = (prev << 1) | 1;
                    }else{
                        branch_predictor[updated_inst.b_index] = (prev >> 1);
                    }
                }

                if(updated_inst.actual_result == 1){
                    branch_history = (branch_history >> 1) | 4;
                }else{
                    branch_history = (branch_history >> 1);
                }

                // For the case when not_taking results in going past even the branch target, we copied the instructions
                // after the branch target even when not taking, in order to roll back the instructions.
                // However, if prediction and actual behavior is equal, there is no need to roll back.
                // The code below simply sets is_not_taking to false to stop copying instructions.
            }
            
            location rs_loc = updated_inst.rs_loc;
            rstation * rs = &funits[rs_loc.fu_type].func_units[rs_loc.funit].rstations[rs_loc.rs_no];
            tag_map[updated_inst.tag_number].update = cycle_counter;
            p_stats->retired_instruction += 1;

            if(rs->dest >= 0){
                // Update the resulting data to CDB
                cdb_lookup[rs->dest].push_back(updated_inst);
            }

            // Reset the reservation station
            rs->busy = false;
            rs->executing = false;
            rs->opcode = -2;
            rs->tag_number = 0;
            rs->dest = -2;
            rs->q1 = -2;
            rs->q2 = -2;
            rs->v1 = -2;
            rs->v2 = -2;

            if(updated_inst.dest >= 0){     // For instructions that has a target register to send output
                // Free up the register for new inst. 
                // Achieves both indicating that the value in the register is ready and opening up the register for new writes
                registers[updated_inst.dest].tag_number = (uint64_t) 0;
                registers[updated_inst.dest].busy = false;
            }
        }

        update_queue.clear();   // Clear the update queue, and wait for new results from the latter part of the cycle
    }
}

/**
 * Simulates the processor.
 * fetch instructions as appropriate, until all instructions have executed
 *
 * @p_stats Pointer to the statistics structure
 */
void run_processor(processor_stats_t* p_stats) {

    //for(int i = 0; i < 50; i++){
    while(true){

        cycle_counter += 1;

        update(p_stats);
        execute(p_stats);
        schedule(p_stats);
        dispatch(p_stats);
        fetch(p_stats);

        // From the map that stores the tags, if there are tags that are ready to be printed, print it.
        // Then delete the item from the map to save space.
        // Breaks if the front of the map (sorted by the keys) is not ready to be printed out.
        std::map<uint64_t, tag>::iterator it = tag_map.begin();

        while(it != tag_map.end()){     
            if(it->second.update == 0){
                break;
            }else{
                tag result_tag = it->second;
                printf("%u ", it->first);
                printf(" %u ", result_tag.fetch);
                printf(" %u ", result_tag.dispatch);
                printf(" %u ", result_tag.schedule);
                printf(" %u ", result_tag.execute);
                printf(" %u\n", result_tag.update);

                it = tag_map.erase(it);     // Erases the element from the map.
            }
        }

        // If all the tags are printed, return. Since all the instructions are added as soon as the instruction is fetched,
        // this ensures that all the tags are printed when update cycle has been recorded.
        if(tag_map.size() == 0){
            return;
        }
    }
}

/**
 * Subroutine for cleaning up any outstanding instructions and calculating overall statistics
 * such as average IPC or branch prediction percentage
 *
 * @p_stats Pointer to the statistics structure
 */
void complete_processor(processor_stats_t *p_stats) {

    p_stats->cycle_count = cycle_counter;
    p_stats->avg_d_queue = avg_d_queue;
    p_stats->avg_ipc = (float) p_stats->inst_issued / p_stats->cycle_count;
    p_stats->avg_inst_fire = (float) p_stats->inst_fired / p_stats->cycle_count;
    p_stats->avg_inst_retired = (float) p_stats->retired_instruction / p_stats->cycle_count;
    p_stats->perc_branch_pred = (float) p_stats->b_inst_correct / p_stats->b_inst_count;

    // Free up the memory, check for potential memory leaks.
    for(int i = 0; i < 3; i++){
        uint64_t no_fu = funits[0].no_fu;

        for(int k = 0; k < no_fu; k++){
            delete [] funits[i].func_units[k].rstations;
        }
        delete [] funits[i].func_units;
    }
}
