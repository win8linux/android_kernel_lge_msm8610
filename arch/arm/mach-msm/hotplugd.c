/*
 * arch/arm/mach-msm/hotplugd.c
 *
 * Simple cpu hotplug daemon. It enables/disables cpus based on cpu frequencies.
 * Cpu governor should be properly tuned to use best frequencies for power efficiency.
 *
 * Loosely based on ASMP hotplug by Rauf Gungor (mrg666).
 * 
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/moduleparam.h>
#include <linux/cpufreq.h>
#include <linux/workqueue.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/hrtimer.h>

#define HOTPLUGD_TAG "HOTPLUGD: "
#define hotplugd_STARTDELAY 20000

#define MAX_SCALING_FREQ 1190400
#define DELAY_LOW 600 // millis
#define DELAY_HIGH 1000

static unsigned int LAST_MAX_FREQ = 0;
static unsigned int LAST_DELAY = 0;
static unsigned int MAX_CPUS = CONFIG_NR_CPUS;

static struct delayed_work hotplugd_work;
static struct workqueue_struct *hotplugd_workq;

static int enabled = 1;
	

static void bringup_cpu(void) {
	
	int cpu = cpumask_next_zero(0, cpu_online_mask);	
	if (cpu <= (MAX_CPUS-1)) 
		cpu_up(cpu);
}

static void bringdown_cpu(void) {
	
	int cpu = cpumask_next(0, cpu_online_mask);	
	if (cpu <= (MAX_CPUS-1)) 
		cpu_down(cpu);
}

static void __cpuinit hotplugd_work_fn(struct work_struct *work) {
	unsigned int cpu = 0 ;
	unsigned int current_delay, current_freq;
	unsigned int MAX_FREQ = 0;
	for_each_online_cpu(cpu) {
		current_freq = cpufreq_quick_get(cpu);
		if (current_freq > MAX_FREQ)
			MAX_FREQ = current_freq;
	}	/* Find the maximum Frequency accross cpus*/

	if (MAX_FREQ == MAX_SCALING_FREQ) { 
		for_each_cpu_not(cpu,cpu_online_mask) {
			if (cpu <= (MAX_CPUS - 1))
				cpu_up(cpu);
		}	
		current_delay = DELAY_LOW;
		goto reschedule;				
	}
	
	switch (LAST_DELAY) {
		case DELAY_LOW:
			if (MAX_FREQ > LAST_MAX_FREQ) { 
				bringup_cpu();
				current_delay = DELAY_LOW;
				goto reschedule;
			}
			if (MAX_FREQ == LAST_MAX_FREQ) { 	
				current_delay = DELAY_HIGH ;	
				goto reschedule;
			}
			if (MAX_FREQ < LAST_MAX_FREQ) {
				bringdown_cpu();
				current_delay = DELAY_LOW;	
				goto reschedule;
			}
		case DELAY_HIGH:
			if (MAX_FREQ > LAST_MAX_FREQ) { 
				bringup_cpu();
				current_delay = DELAY_LOW;
				goto reschedule;
			}
			if (MAX_FREQ == LAST_MAX_FREQ) {
				bringdown_cpu();
				current_delay = DELAY_LOW;
				goto reschedule;
			}
			if (MAX_FREQ < LAST_MAX_FREQ) { 
				current_delay = DELAY_HIGH;	
				bringdown_cpu();
				goto reschedule;
			}
	}


reschedule:
	LAST_MAX_FREQ = MAX_FREQ ;
	LAST_DELAY = current_delay;
	queue_delayed_work(hotplugd_workq, &hotplugd_work,
			   msecs_to_jiffies(current_delay));
}


static int __cpuinit set_enabled(const char *val, const struct kernel_param *kp) {
	int ret;
	int cpu;

	ret = param_set_bool(val, kp);
	if (enabled) {
		queue_delayed_work(hotplugd_workq, &hotplugd_work,
				msecs_to_jiffies(DELAY_LOW));
		pr_info(HOTPLUGD_TAG"enabled\n");
	} else {
		cancel_delayed_work_sync(&hotplugd_work);
		for (cpu = 1; cpu < nr_cpu_ids; cpu++)
			if (!cpu_online(cpu)) 
				cpu_up(cpu);
		pr_info(HOTPLUGD_TAG"disabled\n");
	}
	return ret;
}

static struct kernel_param_ops module_ops = {
	.set = set_enabled,
	.get = param_get_bool,
};

module_param_cb(enabled, &module_ops, &enabled, 0644);
MODULE_PARM_DESC(enabled, "hotplugd");

static int __init hotplugd_init(void) {

	hotplugd_workq = create_freezable_workqueue("hotplugd");
	if (!hotplugd_workq)
		return -ENOMEM;
	INIT_DELAYED_WORK(&hotplugd_work, hotplugd_work_fn);
	if (enabled)
		queue_delayed_work(hotplugd_workq, &hotplugd_work,
				   msecs_to_jiffies(hotplugd_STARTDELAY));
	pr_info(HOTPLUGD_TAG"initialized\n");
	return 0;
}
late_initcall(hotplugd_init);
