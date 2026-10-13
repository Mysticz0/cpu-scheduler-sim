#include "scheduler.h"

#include <stdio.h>
#include <stdlib.h>

/*
 * This is the source file you must complete.
 *
 * You are responsible for implementing the simulated operating-system
 * behaviour, including:
 *   - PCBs and process states;
 *   - the ready queue and waiting processes;
 *   - CPU and I/O burst accounting;
 *   - FCFS, Round Robin, and preemptive SRTF;
 *   - dispatch/context-switch overhead; and
 *   - calls to the supplied metrics and CSV logging functions.
 *
 * You may replace this file completely and add any .c/.h files you need.
 * The types and helper functions below are only a suggested decomposition.
 * You may rename, replace, combine, or remove them.
 */

/*
 * Suggested PCB starting point. Add the fields required by the handout, such
 * as remaining CPU time, completed bursts, I/O completion time, Ready-entry
 * order, and RR quantum usage.
 */
typedef struct {
    ProcessSpec spec;
    ProcessState state;
    int bursts_complete;
    int remaining_time_cpu_burst;
    int io_completion_time;
    int quantum_used;
    /* TODO: add the rest of your PCB fields. */
} PCB;

typedef struct {
    PCB *ready_processes[MAX_PROCESSES];
    int front;
    int rear;
    int size;
} Queue;

PCB *remove_pcb_in_queue(Queue *ready_queue, int position);
bool queue_is_empty(const Queue *ready_queue);
int find_shortest_ready_burst(const Queue *ready_queue);
void ready_enqueue(PCB *pcb, Queue *ready_queue);
void print_pcb(PCB *current_pcb);
bool initialize_pcbs(PCB *pcbs, const ProcessSpec *specs, size_t count);
void print_pcbs(PCB *pcbs, size_t count);
bool is_process_running(PCB *pcbs, size_t count);
bool are_pcbs_terminated(PCB *pcbs, size_t count);
void admit_arrivals(PCB *pcbs, size_t count, size_t time, OutputContext *output, Queue *ready_queue);
void complete_io(PCB *pcbs, size_t count, size_t time, OutputContext *output, Queue *ready_queue);
void resolve_cpu_interval(PCB *pcbs, size_t count, size_t time, OutputContext *output, Queue *ready_queue, TimelineActivity *cpu_status, const SimulationConfig *config);
void advance_one_interval(PCB *pcbs, size_t count, size_t time, OutputContext *output, TimelineActivity cpu_status, int *context_switch);
void choose_next_process(size_t time, OutputContext *output, Queue *ready_queue, Algorithm algorithm);
void update_dispatch(size_t time, OutputContext *output, Queue *ready_queue, TimelineActivity *cpu_status, int *context_switch, const SimulationConfig *config);
void maybe_preempt_srtf(PCB *pcbs, size_t count, size_t time, OutputContext *output, Queue *ready_queue, TimelineActivity *cpu_status, Algorithm algorithm);

/* You may choose to maintain a structure for your queues.
 * Do so in whichever way you see fit.
 */

/* Keeping whole-simulation state together makes helper signatures smaller. 
 * Store whatever you feel is necessary for your implementation.
 */
typedef struct {
    size_t process_count;
} Simulation;

/*
 * Supplied output API examples
 * ----------------------------
 * These functions are declared in scheduler.h and implemented in io.c. They
 * format the CSV rows for you and calculate the metrics. You must use them
 * to ensure your output can be correctly processed by the test harness.
 *
 * State transition:
 *
 *   log_transition(output, time, pid,
 *                  STATE_NEW, STATE_READY, REASON_ARRIVAL);
 *
 * One timeline interval [time, time + 1):
 *
 *  If the CPU is running a process:
 *      record_timeline_tick(output, time, ACTIVITY_CPU, running_pid);
 *
 *  If the CPU is performing a context switch:
 *      record_timeline_tick(output, time, ACTIVITY_CONTEXT_SWITCH, 0);
 *
 *  If the CPU is idle:
 *      record_timeline_tick(output, time, ACTIVITY_IDLE, 0);
 *
 * Final metrics (one call after every process terminates):
 *
 *   if (!write_metrics(output, specs, count)) {
 *       return false;
 *   }
 *
 */

/*
 * Suggested helper decomposition (optional)
 * -----------------------------------------
 * You may find helpers with responsibilities like these useful:
 *
 *   ✔️ initialize_pcbs(...)
 *   ✔️ ready_enqueue(...)
 *   ✔️ choose_next_process(...)
 *   ✔️ admit_arrivals(...)
 *   ✔️ complete_io(...)
 *   maybe_preempt_srtf(...)
 *   update_dispatch(...)
 *   advance_one_interval(...)
 *
 * Required event-loop order
 * -------------------------
 *
 *   while not all processes are terminated:
 *       resolve the CPU interval that ended at the current time
 *       admit arrivals in ascending PID order
 *       complete I/O in ascending PID order
 *       apply the SRTF preemption rule, if applicable
 *       complete or begin dispatch/context-switch activity
 *       record exactly one CPU, context-switch, or idle timeline tick
 *       advance time by one
 *
 * You must design and implement the loop yourself. The reference outputs care
 * about observable timing and ordering, not the names of your private helpers.
 */

bool queue_is_empty(const Queue *ready_queue){
    return ready_queue->size == 0;
}

bool initialize_pcbs(PCB *pcbs, const ProcessSpec *specs, size_t count){
    if (pcbs == NULL) {
        fprintf(stderr, "Initialization Failed!");
        return false;
    }

    for (size_t i = 0; i < count; i++){
        pcbs[i].spec = specs[i];
        pcbs[i].state = STATE_NEW;
        pcbs[i].bursts_complete = 0;
        pcbs[i].remaining_time_cpu_burst = pcbs[i].spec.cpu_burst;
        pcbs[i].io_completion_time = pcbs[i].spec.arrival_time + pcbs[i].spec.cpu_burst + pcbs[i].spec.io_burst;
        pcbs[i].quantum_used = 0;
    }
    return true;
}


void print_pcb(PCB *current_pcb){
    printf("PID = %d\n"
           "ARRIVAL TIME = %d\n"
           "CPU BURST = %d\n" 
           "IO BURST = %d\n" 
           "NUM CPU BURSTS = %d\n" 
           "STATE = %d\n" 
           "COMPLETED CPU BURSTS = %d\n" 
           "TIME REMAINING IN CURRENT BURST = %d\n" 
           "QUANTUM USED = %d", 
        current_pcb->spec.pid, 
        current_pcb->spec.arrival_time, 
        current_pcb->spec.cpu_burst, 
        current_pcb->spec.io_burst, 
        current_pcb->spec.num_cpu_bursts, 
        current_pcb->state,
        current_pcb->bursts_complete,
        current_pcb->remaining_time_cpu_burst,
        current_pcb->quantum_used);
}

void print_pcbs(PCB *pcbs, size_t count){
    for (size_t i = 0; i < count; i++){
        print_pcb(&pcbs[i]);
    }
}

bool is_cpu_busy(TimelineActivity cpu_status){
    if (cpu_status == ACTIVITY_CPU) return true;
    return false;
}

bool are_pcbs_terminated(PCB *pcbs, size_t count){
    for (size_t i = 0; i < count; i++){
        if (pcbs[i].state != STATE_TERMINATED) return false;
    }
    return true;
}

void ready_enqueue(PCB *pcb, Queue *ready_queue){
    ready_queue->rear = (ready_queue->rear + 1) % MAX_PROCESSES;
    ready_queue->ready_processes[ready_queue->rear] = pcb;
    ready_queue->size++;
    printf("ENQUEUED PCB %d\n", pcb->spec.pid);
}

PCB *remove_pcb_in_queue(Queue *ready_queue, int position){
    int index = (ready_queue->front + position) % MAX_PROCESSES;
    PCB *pcb = ready_queue->ready_processes[index];

    for (int i = position; i < ready_queue-> size - 1; i++){
        int current = (ready_queue->front + i) % MAX_PROCESSES;
        int next = (ready_queue->front + i + 1) % MAX_PROCESSES;
        ready_queue->ready_processes[current] = ready_queue->ready_processes[next];
    }

    ready_queue->rear = (ready_queue->rear - 1 + MAX_PROCESSES) % MAX_PROCESSES;
    ready_queue->size--;
    return pcb;
}

void choose_next_process(size_t time, OutputContext *output, Queue *ready_queue, Algorithm algorithm){
    int position = 0;
    if (algorithm == ALG_SRTF) position = find_shortest_ready_burst(ready_queue);

    PCB *next = remove_pcb_in_queue(ready_queue, position);
    ProcessState prevState = next->state;

    next->state = STATE_RUNNING;
    next->quantum_used = 0;
    printf("PCB %d RUNNING\n", next->spec.pid);
    log_transition(output, time, next->spec.pid, prevState, STATE_RUNNING, REASON_DISPATCH);
}

void admit_arrivals(PCB *pcbs, 
                    size_t count, 
                    size_t time, 
                    OutputContext *output, 
                    Queue *ready_queue)
{
    ProcessState prevState;

    for (size_t i = 0; i < count; i++){
        if (pcbs[i].state == STATE_NEW && pcbs[i].spec.arrival_time == time){
            prevState = pcbs[i].state;
            pcbs[i].state = STATE_READY;
            printf("PCB %d READY\n", pcbs[i].spec.pid);
            log_transition(output, time, pcbs[i].spec.pid, prevState, STATE_READY, REASON_ARRIVAL);
            ready_enqueue(&pcbs[i], ready_queue);
        }
    }

}

void resolve_cpu_interval(PCB *pcbs, size_t count, size_t time, OutputContext *output, Queue *ready_queue, TimelineActivity *cpu_status, const SimulationConfig *config){
    if (*cpu_status != ACTIVITY_CPU) return;

    PCB *running_pcb = NULL;
    for (size_t i = 0; i < count; i++){
        if (pcbs[i].state == STATE_RUNNING){
            running_pcb = &pcbs[i];
            break;
        }
    }
    if (running_pcb == NULL) return;

    
    if (running_pcb->remaining_time_cpu_burst == 0){
        running_pcb->bursts_complete++;

        if (running_pcb->bursts_complete < running_pcb->spec.num_cpu_bursts){
            log_transition(output, time, running_pcb->spec.pid, running_pcb->state, STATE_WAITING, REASON_CPU_BURST_COMPLETE);
            running_pcb->state = STATE_WAITING;
            running_pcb->io_completion_time = time + running_pcb->spec.io_burst;
            running_pcb->remaining_time_cpu_burst = running_pcb->spec.cpu_burst;
            printf("PCB %d WAITING\n", running_pcb->spec.pid);
        } else {
            log_transition(output, time, running_pcb->spec.pid, running_pcb->state, STATE_TERMINATED, REASON_PROCESS_COMPLETE);
            running_pcb->state = STATE_TERMINATED;
            printf("PCB %d TERMINATED\n", running_pcb->spec.pid);
        }

        *cpu_status = ACTIVITY_IDLE;
        
    } else if (config->algorithm == ALG_RR && running_pcb->quantum_used == config->quantum){
        log_transition(output, time, running_pcb->spec.pid, running_pcb->state, STATE_READY, REASON_QUANTUM_EXPIRED);
        running_pcb->state = STATE_READY;
        ready_enqueue(running_pcb, ready_queue);
        *cpu_status = ACTIVITY_IDLE;
    }
}

void complete_io(PCB *pcbs, size_t count, size_t time, OutputContext *output, Queue *ready_queue) {
    for (size_t i = 0; i < count; i++){
        if (pcbs[i].state == STATE_WAITING && pcbs[i].io_completion_time == time){
            log_transition(output, time, pcbs[i].spec.pid, pcbs[i].state, STATE_READY, REASON_IO_COMPLETE);
            pcbs[i].state = STATE_READY;
            ready_enqueue(&pcbs[i], ready_queue);
        }
    }
}

void advance_one_interval(PCB *pcbs, size_t count, size_t time, OutputContext *output, TimelineActivity cpu_status, int *context_switch) {
    switch (cpu_status) {
        case ACTIVITY_CPU:
            for (size_t i = 0; i < count; i++) {
                if (pcbs[i].state == STATE_RUNNING) {
                    pcbs[i].remaining_time_cpu_burst--;
                    pcbs[i].quantum_used++;
                    record_timeline_tick(output, time, ACTIVITY_CPU, pcbs[i].spec.pid);
                    break;
                }
            }
            break;

        case ACTIVITY_CONTEXT_SWITCH:
            (*context_switch)--;
            record_timeline_tick(output, time, ACTIVITY_CONTEXT_SWITCH, 0);
            break;

        case ACTIVITY_IDLE:
            record_timeline_tick(output, time, ACTIVITY_IDLE, 0);
            break;
    }
}

void update_dispatch(size_t time, OutputContext *output, Queue *ready_queue, TimelineActivity *cpu_status, int *context_switch, const SimulationConfig *config) {
    if (*cpu_status == ACTIVITY_IDLE && !queue_is_empty(ready_queue)) {
        *context_switch = config->context_switch;
        *cpu_status = ACTIVITY_CONTEXT_SWITCH;
    }
    if (*cpu_status == ACTIVITY_CONTEXT_SWITCH && *context_switch == 0) {
        choose_next_process(time, output, ready_queue, config->algorithm);
        *cpu_status = ACTIVITY_CPU;
    }
}

int find_shortest_ready_burst(const Queue *ready_queue){
    int best = -1;
    for (int i = 0; i < ready_queue->size; i++){
        PCB *pcb = ready_queue->ready_processes[(ready_queue->front + i) % MAX_PROCESSES];
        PCB *best_pcb = NULL;
        if (best >= 0) best_pcb = ready_queue->ready_processes[(ready_queue->front + best) % MAX_PROCESSES];
        if (best_pcb == NULL || pcb->remaining_time_cpu_burst < best_pcb->remaining_time_cpu_burst) best = i;
    }
    return best;
}

void maybe_preempt_srtf(PCB *pcbs, size_t count, size_t time, OutputContext *output, Queue *ready_queue, TimelineActivity *cpu_status, Algorithm algorithm){
    if (algorithm != ALG_SRTF || *cpu_status != ACTIVITY_CPU) return;

    int pcb_position = find_shortest_ready_burst(ready_queue);
    if (pcb_position < 0) return;
    PCB *shortest_burst_pcb = ready_queue->ready_processes[(ready_queue->front + pcb_position) % MAX_PROCESSES];

    PCB *running_pcb = NULL;
    for (size_t i = 0; i < count; i++){
        if (pcbs[i].state == STATE_RUNNING){
            running_pcb = &pcbs[i];
            break;
        }
    }
    if (running_pcb == NULL) return;
    
    
    if (shortest_burst_pcb->remaining_time_cpu_burst < running_pcb->remaining_time_cpu_burst){
        running_pcb->state = STATE_READY;
        log_transition(output, time, running_pcb->spec.pid, STATE_RUNNING, STATE_READY, REASON_SRTF_PREEMPTION);
        ready_enqueue(running_pcb, ready_queue);
        *cpu_status = ACTIVITY_IDLE;
    }
}


/*
 * Run one complete scheduling simulation.
 *
 * Parameters
 * ----------
 * specs:
 *     A read-only array of validated process descriptions, sorted by ascending
 *     PID. Each entry supplies the arrival time, CPU- and I/O-burst lengths,
 *     and number of CPU bursts for one process. Copy entries into your PCBs if
 *     you need mutable per-process state. Do not modify or free this array.
 *
 * count:
 *     The number of ProcessSpec entries in specs.
 *
 * config:
 *     A read-only SimulationConfig containing the selected algorithm, the RR
 *     quantum, and the context-switch duration. The quantum affects scheduling
 *     only when config->algorithm is ALG_RR. Do not modify or free config.
 *
 * output:
 *     An already-open output context. Pass it to log_transition(),
 *     record_timeline_tick(), and write_metrics() as described above. main.c
 *     owns this context and will close it; do not close or free it here.
 *
 * Return true after the simulation and final metrics are written successfully.
 * Return false if initialization, simulation, or output generation fails.
 * main.c retains ownership of specs, config, and output in either case.
 */
bool run_simulation(const ProcessSpec *specs,
                    size_t count,
                    const SimulationConfig *config,
                    OutputContext *output)
{

    // Initialize PCBs
    PCB *pcbs = malloc(count * sizeof(PCB));
    if (!initialize_pcbs(pcbs, specs, count)) return false;

    print_pcbs(pcbs, count);
    printf("\n\n======================================================\n\n");

    // Main Simulation Loop
    size_t time = 0;
    Queue ready_queue;
    ready_queue.front = 0;
    ready_queue.rear = -1;
    ready_queue.size = 0;
    TimelineActivity cpu_status = ACTIVITY_IDLE;
    int context_switch = 0;

    while (!are_pcbs_terminated(pcbs, count)) {                                                               // while not all processes are terminated:
        printf("TICK: %ld\n", time);
        resolve_cpu_interval(pcbs, count, time, output, &ready_queue, &cpu_status, config);                   // resolve the CPU interval that ended at the current time
        if (are_pcbs_terminated(pcbs, count)) break;
        admit_arrivals(pcbs, count, time, output, &ready_queue);                                              // admit arrivals in ascending PID order
        complete_io(pcbs, count, time, output, &ready_queue);                                                 // complete I/O in ascending PID order
        maybe_preempt_srtf(pcbs, count, time, output, &ready_queue, &cpu_status, config->algorithm);          // apply the SRTF preemption rule, if applicable
        update_dispatch(time, output, &ready_queue, &cpu_status, &context_switch, config);                    // complete or begin dispatch/context-switch activity 
        advance_one_interval(pcbs, count, time, output, cpu_status, &context_switch);                         // record exactly one CPU, context-switch, or idle timeline tick
        time++;                                                                                               // advance time by one
    }

    free(pcbs);
    return write_metrics(output, specs, count);
}
