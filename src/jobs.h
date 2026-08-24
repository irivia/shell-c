#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <wait.h>
#include <poll.h>
#include "lexer.h"
#include "da.h"

typedef struct {
    int pid;
    int idx;
    int fds[2];
    char **cmd;
} Job;

typedef struct {
    Job *items;
    size_t count;
    size_t capacity;
    size_t ptr;
} JobList;

static void job_free(Job *job)
{
    if (!job) return;

    free_cstrlist(&job->cmd);
}

static void get_recent_job_idx(JobList *jobs, size_t *one, size_t *two)
{
    *one = 0;
    *two = 0;
    for (size_t i = 0; i < jobs->count; i++) {
        Job job = jobs->items[i];
        if (job.idx > *one) {
            *two = *one;
            *one = job.idx;
        }
    }
}

static void print_job(JobList *jobs, Job job, bool done)
{
    size_t highest_idx = 0;
    size_t second_highest_idx = 0;
    get_recent_job_idx(jobs, &highest_idx, &second_highest_idx);
    char marker = ' ';
    if (job.idx == highest_idx)
        marker = '+';
    else if (job.idx == second_highest_idx)
        marker = '-';
    printf("[%d]%c  %s                 ", job.idx, marker, done ? "Done" : "Running");
    for (size_t j = 0; job.cmd[j]; j++) {
        printf("%s ", job.cmd[j]);
    }
    printf("%s", done ? "\n" : "&\n");
    fflush(stdout);
}

static void print_jobs(JobList *jobs)
{
    if (!jobs->count) {
        printf("No jobs are currently running!\n");
        fflush(stdout);
        return;
    }
    da_foreach(*jobs, job) {
        print_job(jobs, *job, false);
    }
}

static void poll_jobs(JobList *jobs, int time_out_ms)
{
    if (!jobs->count) return;

    struct pollfd fds[jobs->count];
    for (size_t i = 0; i < jobs->count; i++) {
        fds[i].fd = jobs->items[i].fds[0];
        fds[i].events = POLLIN;
    }
    int ret = poll(fds, jobs->count, time_out_ms);
    if (ret <= 0) return;
    for (size_t i = 0; i < jobs->count; i++) {
        if (~fds[i].revents & POLLIN) continue;

        Job job = jobs->items[i];
        char buffer[4096];
        StringBuilder str = {0};
        const size_t buffer_sz = sizeof(buffer);
        ssize_t bytes;
        while ((bytes = read(job.fds[0], buffer, buffer_sz)) > 0) {
            for (ssize_t i = 0; i < bytes; i++) {
                da_push(str, buffer[i]);
            }
        }
        if (str.count) {
            da_push(str, '\0');
            if (str.items[str.count - 2] == '\n')
                printf("%s", str.items);
            else
                printf("%s\n", str.items);
            fflush(stdout);
        }
        da_free(str);
    }
}

static void update_jobs(JobList *jobs, int *jobs_idx)
{
    for (size_t i = 0; i < jobs->count;) {
        Job job = jobs->items[i];
        if (waitpid(job.pid, NULL, WNOHANG) != 0) {
            print_job(jobs, job, true);
            da_remove(*jobs, i);
            close(job.fds[0]);
            job_free(&job);
            *jobs_idx -= 1;
            continue;
        }
        i++;
    }
}
