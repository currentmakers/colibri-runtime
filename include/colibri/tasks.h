#ifndef COLIBRI_RUNTIME_TASKS_H
#define COLIBRI_RUNTIME_TASKS_H

void io_initialize(int number_of_slots);
void supervisor_initialize();
void io_schedule(struct k_work_delayable* work, k_timeout_t timeout);


void rgb_updater_initialize();

#endif //COLIBRI_RUNTIME_TASKS_H
