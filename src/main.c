/* main.c - Hello World demo */

/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>


/* ADC STUFF */
	#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) || \
		!DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
	#error "No suitable devicetree overlay specified"
	#endif

	#define DT_SPEC_AND_COMMA(node_id, prop, idx) \
		ADC_DT_SPEC_GET_BY_IDX(node_id, idx),

	/* Data of ADC io-channels specified in devicetree. */
	static const struct adc_dt_spec adc_channels[] = {
		DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), io_channels,
					DT_SPEC_AND_COMMA)
	};
			int err;
		uint32_t count = 0;
		uint16_t buf;
		struct adc_sequence sequence = {
			.buffer = &buf,
			/* buffer size in bytes, not number of samples */
			.buffer_size = sizeof(buf),
		};


/* THREADS STUFF */
	#define PIN_THREADS (IS_ENABLED(CONFIG_SMP) && IS_ENABLED(CONFIG_SCHED_CPU_MASK))
	/* size of stack area used by each thread */
	#define STACKSIZE 1024
	/* scheduling priority used by each thread */
	#define PRIORITY 7
	/* delay between greetings (in ms) */
	#define SLEEPTIME 500

/* LED GPIO STUFF */
	/* The devicetree node identifier for the "led0" alias. */
	#define LED0_NODE DT_ALIAS(led0)
/*
 * A build error on this line means your board is unsupported.
 * See the sample documentation for information on how to fix this.
 */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
/*
 * @param my_name      thread identification string
 * @param my_sem       thread's own semaphore
 * @param other_sem    other thread's semaphore
 */
void helloLoop(const char *my_name,
	       struct k_sem *my_sem, struct k_sem *other_sem)
{
	const char *tname;
	uint8_t cpu;
	struct k_thread *current_thread;


	while (1) {
		/* take my semaphore */
		k_sem_take(my_sem, K_FOREVER);

		current_thread = k_current_get();
		tname = k_thread_name_get(current_thread);
#if CONFIG_SMP
		cpu = arch_curr_cpu()->id;
#else
		cpu = 0;
#endif
		/* say "hello" */
		if (tname == NULL) {
			printk("%s: Hello World from cpu %d on %s!\n",
				my_name, cpu, CONFIG_BOARD);
		} else {
			printk("%s: Hello World from cpu %d on %s!\n",
				tname, cpu, CONFIG_BOARD);
		}

		/* TOGGLE LED*/
		gpio_pin_toggle_dt(&led);

		/* ADC READS */
		printk("ADC reading[%u]:\n", count++);
		for (size_t i = 0U; i < ARRAY_SIZE(adc_channels); i++) {
			int32_t val_mv;

			printk("- %s, channel %d: ",
			       adc_channels[i].dev->name,
			       adc_channels[i].channel_id);

			(void)adc_sequence_init_dt(&adc_channels[i], &sequence);

			err = adc_read(adc_channels[i].dev, &sequence);
			if (err < 0) {
				printk("Could not read (%d)\n", err);
				continue;
			}

			/*
			 * If using differential mode, the 16 bit value
			 * in the ADC sample buffer should be a signed 2's
			 * complement value.
			 */
			if (adc_channels[i].channel_cfg.differential) {
				val_mv = (int32_t)((int16_t)buf);
			} else {
				val_mv = (int32_t)buf;
			}
			printk("%"PRId32, val_mv);
			err = adc_raw_to_millivolts_dt(&adc_channels[i],
						       &val_mv);
			/* conversion to mV may not be supported, skip if not */
			if (err < 0) {
				printk(" (value in mV not available)\n");
			} else {
				printk(" = %"PRId32" mV\n", val_mv);
			}
		}

		/* wait a while, then let other thread have a turn */
		k_busy_wait(1000000);
		k_msleep(SLEEPTIME);
		k_sem_give(other_sem);
	}
}

/* define semaphores */

K_SEM_DEFINE(threadA_sem, 1, 1);	/* starts off "available" */
K_SEM_DEFINE(threadB_sem, 0, 1);	/* starts off "not available" */


/* threadB is a dynamic thread that is spawned by threadA */

void threadB(void *dummy1, void *dummy2, void *dummy3)
{
	ARG_UNUSED(dummy1);
	ARG_UNUSED(dummy2);
	ARG_UNUSED(dummy3);

	/* invoke routine to ping-pong hello messages with threadA */
	helloLoop(__func__, &threadB_sem, &threadA_sem);
}

K_THREAD_STACK_DEFINE(threadA_stack_area, STACKSIZE);
static struct k_thread threadA_data;

K_THREAD_STACK_DEFINE(threadB_stack_area, STACKSIZE);
static struct k_thread threadB_data;

/* threadA is a static thread that is spawned automatically */

void threadA(void *dummy1, void *dummy2, void *dummy3)
{
	ARG_UNUSED(dummy1);
	ARG_UNUSED(dummy2);
	ARG_UNUSED(dummy3);

	/* invoke routine to ping-pong hello messages with threadB */
	helloLoop(__func__, &threadA_sem, &threadB_sem);
}

int main(void)
{
	/* ADC INIT*/

		/* Configure channels individually prior to sampling. */
		for (size_t i = 0U; i < ARRAY_SIZE(adc_channels); i++) {
			if (!device_is_ready(adc_channels[i].dev)) {
				printk("ADC controller device %s not ready\n", adc_channels[i].dev->name);
				return 0;
			}

			err = adc_channel_setup_dt(&adc_channels[i]);
			if (err < 0) {
				printk("Could not setup channel #%d (%d)\n", i, err);
				return 0;
			}
		}
	/* GPIO LED INIT */
		if (!gpio_is_ready_dt(&led)) {
			return 0;
		}
		int ret;
		ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
		if (ret < 0) {
			return 0;
		}

	/* THREADS INIT */
		k_thread_create(&threadA_data, threadA_stack_area,
				K_THREAD_STACK_SIZEOF(threadA_stack_area),
				threadA, NULL, NULL, NULL,
				PRIORITY, 0, K_FOREVER);
		k_thread_name_set(&threadA_data, "thread_a");
	#if PIN_THREADS
		if (arch_num_cpus() > 1) {
			k_thread_cpu_pin(&threadA_data, 0);
		}
	#endif

		k_thread_create(&threadB_data, threadB_stack_area,
				K_THREAD_STACK_SIZEOF(threadB_stack_area),
				threadB, NULL, NULL, NULL,
				PRIORITY, 0, K_FOREVER);
		k_thread_name_set(&threadB_data, "thread_b");
	#if PIN_THREADS
		if (arch_num_cpus() > 1) {
			k_thread_cpu_pin(&threadB_data, 1);
		}
	#endif

	k_thread_start(&threadA_data);
	k_thread_start(&threadB_data);
	return 0;
}
