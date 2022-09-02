#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <asm/unistd.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <filesystem>

#include <locale.h>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <vector>
#include <fstream>
#include <chrono>

#ifdef __cplusplus
extern "C"
{
#endif

    struct perf_struct;

    struct counter_pair
    {
        unsigned long id;
        unsigned long val;
    };

    struct counter_arr
    {
        size_t nr;
        struct counter_pair vals[];
    };

    struct perf_struct *init_perf(std::string event_configs_str);
    struct counter_arr *init_counters(struct perf_struct *perf);
    void reset_counter(struct perf_struct *perf);
    void start_counter(struct perf_struct *perf);
    void stop_counter(struct perf_struct *perf);
    void read_counter(struct perf_struct *perf, struct counter_arr *ctrs, std::string output_filename);
    void print_aggregate_counter(struct counter_arr *ctrs);

    std::vector<__u64> convertToLongArray(std::string input)
    {
        std::istringstream stringReader{input};

        std::vector<__u64> result;

        __u64 number;
        string item;
        while (std::getline(stringReader, item, ','))
        {
            number = std::stoll(item, nullptr, 16);
            result.push_back(number);
        }

        return result;
    }

    int perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                        int cpu, int group_fd, unsigned long flags)
    {
        int ret;

        ret = syscall(__NR_perf_event_open, hw_event, pid, cpu,
                      group_fd, flags);
        return ret;
    }

    struct perf_struct
    {
        struct perf_event_attr pe;
        int perf_fd[5];
        unsigned long perf_id[5];
        size_t nr;
        std::chrono::high_resolution_clock::time_point startTime;
        int64_t duration;
    };

    struct perf_struct *init_perf(std::string event_configs_str)
    {
        //__u64 event_configs[] = &(convertToIntArray("0x5301c0, 0x53003c")[0]);
        std::vector<__u64> event_configs = convertToLongArray(event_configs_str);

        struct perf_struct *perf;

        setlocale(LC_NUMERIC, ""); // For comma seperated printouts

        perf = (struct perf_struct *)malloc(sizeof(struct perf_struct));
        if (!perf)
        {
            perror("Unable to initialize perf, failed to allocate");
            exit(errno);
        }

        memset(perf, 0, sizeof(struct perf_struct));

        size_t nr = event_configs.size();
        perf->nr = nr;

        // Instructions
        perf->pe.type = PERF_TYPE_RAW;
        perf->pe.size = sizeof(struct perf_event_attr);
        perf->pe.config = event_configs[0];

        perf->pe.disabled = 1;
        perf->pe.exclude_kernel = 1;
        perf->pe.exclude_hv = 1;
        perf->pe.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_ID;

        // TODO for loop for below operations
        perf->perf_fd[0] = perf_event_open(&perf->pe, 0, -1, -1, 0);
        if (perf->perf_fd[0] == -1)
        {
            fprintf(stderr, "Error opening leader %llx\n", perf->pe.config);
            fprintf(stderr, "Error code is %d:%s\n", errno, strerror(errno));
            exit(EXIT_FAILURE);
        }
        ioctl(perf->perf_fd[0], PERF_EVENT_IOC_ID, &perf->perf_id[0]);

        if (perf->nr < 2)
            return perf;
        // [1] Cycles
        perf->pe.type = PERF_TYPE_RAW;
        perf->pe.config = event_configs[1];
        perf->perf_fd[1] = perf_event_open(&perf->pe, 0, -1, perf->perf_fd[0], 0);
        if (perf->perf_fd[1] == -1)
        {
            fprintf(stderr, "Error opening leader %llx\n", perf->pe.config);
            fprintf(stderr, "Error code is %d:%s\n", errno, strerror(errno));
            exit(EXIT_FAILURE);
        }
        ioctl(perf->perf_fd[1], PERF_EVENT_IOC_ID, &perf->perf_id[1]);

        if (perf->nr < 3)
            return perf;
        // [2] r11D0
        perf->pe.type = PERF_TYPE_RAW;
        perf->pe.config = event_configs[2];
        perf->perf_fd[2] = perf_event_open(&perf->pe, 0, -1, perf->perf_fd[0], 0);
        if (perf->perf_fd[2] == -1)
        {
            fprintf(stderr, "Error opening leader %llx\n", perf->pe.config);
            fprintf(stderr, "Error code is %d:%s\n", errno, strerror(errno));
            exit(EXIT_FAILURE);
        }
        ioctl(perf->perf_fd[2], PERF_EVENT_IOC_ID, &perf->perf_id[2]);

        if (perf->nr < 4)
            return perf;
        // [3] r412e
        perf->pe.type = PERF_TYPE_RAW;
        perf->pe.config = event_configs[3];
        perf->perf_fd[3] = perf_event_open(&perf->pe, 0, -1, perf->perf_fd[0], 0);
        if (perf->perf_fd[3] == -1)
        {
            fprintf(stderr, "Error opening leader %llx\n", perf->pe.config);
            fprintf(stderr, "Error code is %d:%s\n", errno, strerror(errno));
            exit(EXIT_FAILURE);
        }
        ioctl(perf->perf_fd[3], PERF_EVENT_IOC_ID, &perf->perf_id[3]);

        if (perf->nr < 5)
            return perf;
        // [4] r4f2e
        perf->pe.type = PERF_TYPE_RAW;
        perf->pe.config = event_configs[4];
        perf->perf_fd[4] = perf_event_open(&perf->pe, 0, -1, perf->perf_fd[0], 0);
        if (perf->perf_fd[4] == -1)
        {
            fprintf(stderr, "Error opening leader %llx\n", perf->pe.config);
            fprintf(stderr, "Error code is %d:%s\n", errno, strerror(errno));
            exit(EXIT_FAILURE);
        }
        ioctl(perf->perf_fd[4], PERF_EVENT_IOC_ID, &perf->perf_id[4]);

        return perf;
    }

    struct counter_arr *init_counters(struct perf_struct *perf)
    {
        int i;
        struct counter_arr *arr;
        size_t arr_size = sizeof(struct counter_pair) * perf->nr + sizeof(struct counter_arr);
        arr = (struct counter_arr *)malloc(arr_size);
        memset(arr, 0, arr_size);
        arr->nr = perf->nr;

        for (i = 0; i < perf->nr; i++)
        {
            arr->vals[i].id = perf->perf_id[i];
        }

        return arr;
    }

    void reset_counter(struct perf_struct *perf)
    {
        ioctl(perf->perf_fd[0], PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
    }

    void start_counter(struct perf_struct *perf)
    {
        ioctl(perf->perf_fd[0], PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
        perf->startTime = std::chrono::high_resolution_clock::now();
    }

    void stop_counter(struct perf_struct *perf)
    {
        auto t2 = std::chrono::high_resolution_clock::now();
        perf->duration = std::chrono::duration_cast<std::chrono::nanoseconds>(t2-perf->startTime).count();
        ioctl(perf->perf_fd[0], PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);
    }

    void print_counters(unsigned long insts, unsigned long cycle,
                        unsigned long tlb_miss, unsigned long llc_miss, unsigned long llc_refer)
    {
        printf("1:%'15lu\n", insts);
        printf("2:%'15lu\n", cycle);
        printf("3:%'15lu\n", tlb_miss);
        printf("4:%'15lu\n", llc_miss);
        printf("5:%'15lu\n", llc_refer);
    }

    void print_aggregate_counter(struct counter_arr *ctrs)
    {
        print_counters(ctrs->vals[0].val,
                       ctrs->nr >= 2 ? ctrs->vals[1].val : 0UL,
                       ctrs->nr >= 3 ? ctrs->vals[2].val : 0UL,
                       ctrs->nr >= 4 ? ctrs->vals[3].val : 0UL,
                       ctrs->nr >= 5 ? ctrs->vals[4].val : 0UL);
    }

    struct read_format
    {
        unsigned long nr;
        struct
        {
            unsigned long value;
            unsigned long id;
        } values[];
    };

    void read_counter(struct perf_struct *perf, struct counter_arr *ctrs, std::string output_filename)
    {
        struct read_format *buffer;
        size_t buffer_size = sizeof(*buffer) + sizeof(buffer->values[0]) * 5;
        buffer = (struct read_format *)malloc(buffer_size);
        int read_result = read(perf->perf_fd[0], buffer, buffer_size);
        if (read_result < 0)
        {
            std::cout << "Error reading perf file";
        }

        unsigned long insts = 0UL;
        unsigned long cycle = 0UL;
        unsigned long tlb_miss = 0UL;
        unsigned long llc_miss = 0UL;
        unsigned long llc_refer = 0UL;

        for (int i = 0; i < (int)buffer->nr; i++)
        {
            if (buffer->values[i].id == perf->perf_id[0])
            {
                insts = buffer->values[i].value;
                if (ctrs)
                    ctrs->vals[0].val += insts;
            }
            else if (perf->nr >= 2 && buffer->values[i].id == perf->perf_id[1])
            {
                cycle = buffer->values[i].value;
                if (ctrs)
                    ctrs->vals[1].val += cycle;
            }
            else if (perf->nr >= 3 && buffer->values[i].id == perf->perf_id[2])
            {
                tlb_miss = buffer->values[i].value;
                if (ctrs)
                    ctrs->vals[2].val += tlb_miss;
            }
            else if (perf->nr >= 4 && buffer->values[i].id == perf->perf_id[3])
            {
                llc_miss = buffer->values[i].value;
                if (ctrs)
                    ctrs->vals[3].val += llc_miss;
            }
            else if (perf->nr >= 5 && buffer->values[i].id == perf->perf_id[4])
            {
                llc_refer = buffer->values[i].value;
                if (ctrs)
                    ctrs->vals[4].val += llc_refer;
            }
        }
        //print_counters(insts, cycle, tlb_miss, llc_miss, llc_refer);
        
        //Output to file
        ofstream myfile;
        myfile.open (output_filename);
        myfile << perf->duration << "ns\n";
        myfile << insts << "," << cycle << "," << tlb_miss << "," << llc_miss << "," << llc_refer << "\n"; 
        myfile.close();
        
        free(buffer);
    }

#ifdef __cplusplus
};
#endif
