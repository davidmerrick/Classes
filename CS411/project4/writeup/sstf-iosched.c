#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/init.h>

#define HEAD_FWD 1
#define HEAD_BCK 0

struct sstf_data {
	struct list_head queue;
	sector_t head_pos;
	unsigned char direction;
};

static void noop_merged_requests(struct request_queue *q, struct request *rq,
				 struct request *next)
{
	list_del_init(&next->queuelist);
}

static int sstf_dispatch(struct request_queue *q, int force)
{
	struct sstf_data *nd = q->elevator->elevator_data;
     
	if (!list_empty(&nd->queue)) {
		struct request *nextrq, *prevrq, *rq;  

		nextrq = list_entry(nd->queue.next, struct request, queuelist);
		prevrq = list_entry(nd->queue.prev, struct request, queuelist);

		/* Check if there is only one element in list */
		if (nextrq == prevrq) {
			rq = nextrq;
		} else {
			if (nd->direction == HEAD_FWD) {
				if (nextrq->sector > nd->head_pos) {
					rq = nextrq;
				} else {
					nd->direction = HEAD_BCK;
					rq = prevrq;
				}
			} else { /* Head is going backwards */
				if (prevrq->sector < nd->head_pos) {
					rq = prevrq;
				} else {
					nd->direction = HEAD_FWD;
					rq = nextrq;
				}
			}
		}

		list_del_init(&rq->queuelist);
		nd->head_pos = rq->sector + rq->nr_sectors;
		elv_dispatch_sort(q, rq);

		/* Debugging */
		if(rq_data_dir(rq) == 0)
			printk("[SSTF] dsp READ %lu\n",rq->sector);
		else
			printk("[SSTF] dsp WRITE %lu\n",rq->sector);
		return 1;
	}
	return 0;
}

static void sstf_add_request(struct request_queue *q, struct request *rq)
{
	struct sstf_data *sd = q->elevator->elevator_data;

	struct request *rnext, *rprev;
	sector_t next, prev, pos;

	/*
	If the list is empty, just add it.
	*/
	if(list_empty(&sd->queue))  {
		list_add(&rq->queuelist,&sd->queue);
		return;
	}

	rnext = list_entry(sd->queue.next, struct request, queuelist);
	rprev = list_entry(sd->queue.prev, struct request, queuelist);
	
	next = rnext->sector;
	prev = rprev->sector;
	pos = rq->sector;

	/* 
	Special case: Only 1 item in the queue 
	*/
	if(prev == next){
		if(pos < next){
			list_add(&rq->queuelist,&sd->queue);
		} else {
			list_add_tail(&rq->queuelist,&sd->queue);
		}
		return;
	}

	while(1){
		//Positioned in the queue in between 2 nodes. Put the request here.
		if(pos > prev && pos < next)
			break;
		//Positioned at right edge of queue
		if(next < prev && pos > prev)
			break;
		//Positioned at left edge of queue
		if(prev > next && pos < next)
			break;
		//Rare (impossible?) case where they're equal
		if(pos == next || pos == prev)
			break;
		if(pos > next){ //Move right
			rprev = rnext;
			prev = next; 
			rnext = list_entry(sd->queue.next, struct request, queuelist);
			next = rnext->sector;
		} else { //Move left
			rnext = rprev;
			next = prev;
			rprev = list_entry(sd->queue.prev, struct request, queuelist);
			prev = rprev->sector;
		}
	}
	__list_add(&rq->queuelist, &rprev->queuelist, &rnext->queuelist);
	/* For debugging: */
	printk(KERN_INFO "[SSTF] add %s %ld",rq->cmd,(long) rq->sector);
}

static int noop_queue_empty(struct request_queue *q)
{
	struct sstf_data *nd = q->elevator->elevator_data;

	return list_empty(&nd->queue);
}

static struct request *
noop_former_request(struct request_queue *q, struct request *rq)
{
	struct sstf_data *nd = q->elevator->elevator_data;

	if (rq->queuelist.prev == &nd->queue)
		return NULL;
	return list_entry(rq->queuelist.prev, struct request, queuelist);
}

static struct request *
noop_latter_request(struct request_queue *q, struct request *rq)
{
	struct sstf_data *nd = q->elevator->elevator_data;

	if (rq->queuelist.next == &nd->queue)
		return NULL;
	return list_entry(rq->queuelist.next, struct request, queuelist);
}

static void *noop_init_queue(struct request_queue *q)
{
	struct sstf_data *nd;
	

	nd = kmalloc_node(sizeof(*nd), GFP_KERNEL, q->node);
	if (!nd)
		return NULL;
	INIT_LIST_HEAD(&nd->queue);
	nd->head_pos = 0;
	/* Initialize head going forward */
	nd->direction = HEAD_FWD;
	return nd;
}

static void noop_exit_queue(elevator_t *e)
{
	struct sstf_data *nd = e->elevator_data;

	BUG_ON(!list_empty(&nd->queue));
	kfree(nd);
}

static struct elevator_type elevator_sstf = {
	.ops = {
		.elevator_merge_req_fn		= noop_merged_requests,
		.elevator_dispatch_fn		= sstf_dispatch,
		.elevator_add_req_fn		= sstf_add_request,
		.elevator_queue_empty_fn	= noop_queue_empty,
		.elevator_former_req_fn		= noop_former_request,
		.elevator_latter_req_fn		= noop_latter_request,
		.elevator_init_fn		= noop_init_queue,
		.elevator_exit_fn		= noop_exit_queue,
  },
	.elevator_name = "sstf",
	.elevator_owner = THIS_MODULE,
};

static int __init noop_init(void)
{
	return elv_register(&elevator_noop);
}

static void __exit noop_exit(void)
{
	elv_unregister(&elevator_noop);
}

module_init(noop_init);
module_exit(noop_exit);

MODULE_AUTHOR("David, Alex, Michael");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SSTF I/O scheduler");
