# tomasulo_sim
This is an implementation of Tomasulo algorithm for processor pipelining

## Compile and Run

Makefile has not been modified, and thus running "g++ -o processor processor.cpp driver.cpp" is sufficient to compile the program.
The execution follows the form "cat tracefile |./processor –f F –k K –l L -m M -r R"

### 5 Stage Pipelined Processor

Most of the descriptions about the functionalities were described in the comments, but I would 
like to reiterate the structure of the program in the README for the sake of convenience.

From "run_processor", update, execute, schedule, dispatch, and fetch are executed in order.
If the corresponding queue for execution is empty, we move on to the next stage.
Since all the update, execute, schedule, dispatch queue are empty, we only fetch the instructions
in the first cycle.

cycle_counter is incremented before the execution of the first stage to indicate that the
program enters the corresponding stage in that cycle.

There are different containers for communicating between the stages. If some of the instructions
stay in the same queue due to resource conflict or data dependency reasons, the instructions are
re-inserted to the containers. Below are the containers used for the stages.

Dispatch Queue: std::queue<inst_tag> d_queue;
Schedule Queue: std::queue<exec_inst> sch_q; 
Update Queue: std::map<uint64_t, exec_inst> update_queue;

Also, reservation stations are created. For the reservation stations, total of r reservation stations
are created for each function unit within each function unit type.
Effectively, there are R*ki entries (reservation stations) for function unit of type k.

Although the different function unit numbers within function unit type are not distinguished in
the trace file, the program explicitly distinguishes different function unit numbers within the
same function unit type by creating array of function units with r number of reservation stations.

For example, f_unit_type funits[3] has 3 f_unit_type with ki number of f_units with r number of
reservation stations.

Once in the reservation stations, instructions are checked if any the reservation stations are ready for
execution. If ready, the instruction is executed inside the function execute() where each instructions
inside the schedule queue are decremented of the cycles_remaining. And if the instructions have spent
enough time (latency) inside the schedule queue, meaning that the instructions have completed executing,
the instructions are added to update_queue to be updated in the next cycle.

Inside update(), for each instructions that have completed execution, the results are broadcasted
to the reservation stations through cdb (cdb_lookup). The WAW hazards are handled by adding the results
with the instruction number (tag_number) to the cdb so that the scheduler can check if correct instruction
is collected.

Since the update() is executed first in the cycle loop, the results are updated inside the scheduler
reservation stations in the same cycle. (As per the Bus Arbitration specification in the exam paper)
Also, because the update queue is a std::map which is sorted in the order of key value (tag_number)
the machine state is deterministic because the updates are done in the order of tag order.

The cycle trace output is in the format as below:
<tag_number> <entering fetch> <entering dispatch> <entering schedule queue> <start execution> <update start cycle>

### Branch prediction

Firstly, Branch prediction result is checked inside fetch().

The 128 entry table of 2-bit bimodal GSelect predictor using index created by appending 3 bit wide GHR
with the 4 bit (1~16) of hashed address value. The smith counters are all set to 01 for every predictor table entry.

3 bit GHR means that 3 branch history is tracked, so this is tracked by branch history variable which
starts from 000 and shifted to the right by 1 bit when branch instruction is calculated and then bitwise
| with 100 when taken and leaving as it is when not-taken.

Branch instruction goes into the dispatch queue and schedule queue checking for the data dependencies of
the source registers. The op_code is changed to 0 since the latency of the branch instruction is 1.

When the branch is done calculating / executing, the result is broadcasted to the fetch & dispatch queue.
If the branch is mispredicted, the stall of one cycle is introduced by injecting delayed queue with correct
instructions. (Since the trace file has all the branch target right next to the branch instruction, 
the insertion of actual instructions to delayed queue is not implemented. Initially this was implemented,
but I found it unnecessary & prone to error since the branch target is right next. The implementation detail
is still found in the comments).

Similar to the update order of non-branch instructions, branch instructions with the lower instruction number
(tag number) is updated first as per the specification in the exam paper.
