// SPDX-License-Identifier: GPL-2.0
/*
 * Anxiety I/O Scheduler
 *
 * Copyright (c) 2019, Tyler Nijmeh <tylernij@gmail.com>
 */

#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>

/* For this many sync requests, perform one async request */
#define	DEFAULT_SYNC_RATIO	(4)

/* Run each batch this many times*/
#define DEFAULT_BATCH_COUNT	(2)

enum {
	SYNC,
	ASYNC
};

struct anxiety_data {
	struct list_head queue[2];

	/* Tunables */
	uint8_t sync_ratio;
	uint8_t batch_count;
};

static inline struct request *anxiety_next_entry(struct list_head *queue)
{
	return list_first_entry(queue, struct request,
		queuelist);
}

static void anxiety_merged_requests(struct request_queue *q, struct request *rq,
		struct request *next)
{
	list_del_init(&next->queuelist);
}

static inline int __anxiety_dispatch(struct request_queue *q,
		struct request *rq)
{
	if (unlikely(!rq))
		return -EINVAL;

	list_del_init(&rq->queuelist);
	elv_dispatch_add_tail(q, rq);

	return 0;
}

static uint16_t anxiety_dispatch_batch(struct request_queue *q)
{
	struct anxiety_data *adata = q->elevator->elevator_data;
	uint8_t i, j;
	uint16_t dispatched = 0;
	int ret;

	/* Perform each batch adata->batch_count many times */
	for (i = 0; i < adata->batch_count; i++) {
		/* Batch sync requests according to tunables */
		for (j = 0; j < adata->sync_ratio; j++) {
			if (list_empty(&adata->queue[SYNC]))
				break;

			ret = __anxiety_dispatch(q,
				anxiety_next_entry(&adata->queue[SYNC]));

			if (!ret)
				dispatched++;
		}

		/* Submit one async request after the sync batch to avoid starvation */
		if (!list_empty(&adata->queue[ASYNC])) {
			ret = __anxiety_dispatch(q,
				anxiety_next_entry(&adata->queue[ASYNC]));

			if (!ret)
				dispatched++;
		}

		/* If we didn't have anything to dispatch; don't batch again */
		if (!dispatched)
			break;
	}

	return dispatched;
}

static uint16_t anxiety_dispatch_drain(struct request_queue *q)
{
	struct anxiety_data *adata = q->elevator->elevator_data;
	uint16_t dispatched = 0;
	int ret;

	/*
	 * Drain out all of the synchronous requests first,
	 * then drain the asynchronous requests.
	 */
	while (!list_empty(&adata->queue[SYNC])) {
		ret = __anxiety_dispatch(q,
			anxiety_next_entry(&adata->queue[SYNC]));

		if (!ret)
			dispatched++;
	}

	while (!list_empty(&adata->queue[ASYNC])) {
		ret = __anxiety_dispatch(q,
			anxiety_next_entry(&adata->queue[ASYNC]));

		if (!ret)
			dispatched++;
	}

	return dispatched;
}

static int anxiety_dispatch(struct request_queue *q, int force)
{
	struct anxiety_data *adata = q->elevator->elevator_data;

	/* Make sure we can even process any requests at all */
	if (list_empty(&adata->queue[SYNC]) &&
		list_empty(&adata->queue[ASYNC]))
		return 0;

	/*
	 * When requested by the elevator, a full queue drain can be
	 * performed in one scheduler dispatch.
	 */
	if (unlikely(force))
		return anxiety_dispatch_drain(q);

	return anxiety_dispatch_batch(q);
}

static void anxiety_add_request(struct request_queue *q, struct request *rq)
{
	const uint8_t dir = rq_is_sync(rq);
	struct anxiety_data *adata = q->elevator->elevator_data;

	list_add_tail(&rq->queuelist, &adata->queue[dir]);
}

static struct request *anxiety_former_request(struct request_queue *q, struct request *rq)
{
	const uint8_t dir = rq_data_dir(rq);

	if (rq->queuelist.prev == &((struct anxiety_data *) q->elevator->elevator_data)->queue[dir])
		return NULL;

	return list_prev_entry(rq, queuelist);
}

static struct request *anxiety_latter_request(struct request_queue *q, struct request *rq)
{
	const uint8_t dir = rq_data_dir(rq);

	if (rq->queuelist.next == &((struct anxiety_data *) q->elevator->elevator_data)->queue[dir])
		return NULL;

	return list_next_entry(rq, queuelist);
}

static int anxiety_init_queue(struct request_queue *q,
		struct elevator_type *elv)
{
	struct anxiety_data *adata;
	struct elevator_queue *eq = elevator_alloc(q, elv);

	if (!eq)
		return -ENOMEM;

	/* Allocate the data */
	adata = kmalloc_node(sizeof(*adata), GFP_KERNEL, q->node);
	if (!adata) {
		kobject_put(&eq->kobj);
		return -ENOMEM;
	}

	/* Set the elevator data */
	eq->elevator_data = adata;

	/* Initialize */
	INIT_LIST_HEAD(&adata->queue[SYNC]);
	INIT_LIST_HEAD(&adata->queue[ASYNC]);
	adata->sync_ratio = DEFAULT_SYNC_RATIO;
	adata->batch_count = DEFAULT_BATCH_COUNT;

	/* Set elevator to Anxiety */
	spin_lock_irq(q->queue_lock);
	q->elevator = eq;
	spin_unlock_irq(q->queue_lock);

	return 0;
}

/* Sysfs access */
static ssize_t anxiety_sync_ratio_show(struct elevator_queue *e, char *page)
{
	struct anxiety_data *adata = e->elevator_data;

	return snprintf(page, PAGE_SIZE, "%u\n", adata->sync_ratio);
}

static ssize_t anxiety_sync_ratio_store(struct elevator_queue *e,
		const char *page, size_t count)
{
	struct anxiety_data *adata = e->elevator_data;
	int ret;

	ret = kstrtou8(page, 0, &adata->sync_ratio);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t anxiety_batch_count_show(struct elevator_queue *e, char *page)
{
	struct anxiety_data *adata = e->elevator_data;

	return snprintf(page, PAGE_SIZE, "%u\n", adata->batch_count);
}

static ssize_t anxiety_batch_count_store(struct elevator_queue *e,
		const char *page, size_t count)
{
	struct anxiety_data *adata = e->elevator_data;
	int ret;

	ret = kstrtou8(page, 0, &adata->batch_count);
	if (ret < 0)
		return ret;

	if (adata->batch_count < 1)
		adata->batch_count = 1;

	return count;
}

static struct elv_fs_entry anxiety_attrs[] = {
	__ATTR(sync_ratio, 0644, anxiety_sync_ratio_show,
		anxiety_sync_ratio_store),
	__ATTR(batch_count, 0644, anxiety_batch_count_show,
		anxiety_batch_count_store),
	__ATTR_NULL
};

static struct elevator_type elevator_anxiety = {
	.ops = {
		.elevator_merge_req_fn	= anxiety_merged_requests,
		.elevator_dispatch_fn	= anxiety_dispatch,
		.elevator_add_req_fn	= anxiety_add_request,
		.elevator_former_req_fn	= anxiety_former_request,
		.elevator_latter_req_fn	= anxiety_latter_request,
		.elevator_init_fn	= anxiety_init_queue,
	},
	.elevator_name = "anxiety",
	.elevator_attrs = anxiety_attrs,
	.elevator_owner = THIS_MODULE,
};

static int __init anxiety_init(void)
{
	return elv_register(&elevator_anxiety);
}

static void __exit anxiety_exit(void)
{
	elv_unregister(&elevator_anxiety);
}

module_init(anxiety_init);
module_exit(anxiety_exit);

MODULE_AUTHOR("Tyler Nijmeh");
MODULE_LICENSE("GPLv3");
MODULE_DESCRIPTION("Anxiety I/O scheduler");
