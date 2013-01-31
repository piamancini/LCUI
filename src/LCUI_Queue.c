/* ***************************************************************************
 * LCUI_Queue.c -- basic queue processing
 * 
 * Copyright (C) 2012 by
 * Liu Chao
 * 
 * This file is part of the LCUI project, and may only be used, modified, and
 * distributed under the terms of the GPLv2.
 * 
 * (GPLv2 is abbreviation of GNU General Public License Version 2)
 * 
 * By continuing to use, modify, or distribute this file you indicate that you
 * have read the license and understand and accept it fully.
 *  
 * The LCUI project is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY 
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GPL v2 for more details.
 * 
 * You should have received a copy of the GPLv2 along with this file. It is 
 * usually in the LICENSE.TXT file, If not, see <http://www.gnu.org/licenses/>.
 * ****************************************************************************/
 
/* ****************************************************************************
 * LCUI_Queue.c -- 基本的队列处理
 *
 * 版权所有 (C) 2012 归属于 
 * 刘超
 * 
 * 这个文件是LCUI项目的一部分，并且只可以根据GPLv2许可协议来使用、更改和发布。
 *
 * (GPLv2 是 GNU通用公共许可证第二版 的英文缩写)
 * 
 * 继续使用、修改或发布本文件，表明您已经阅读并完全理解和接受这个许可协议。
 * 
 * LCUI 项目是基于使用目的而加以散布的，但不负任何担保责任，甚至没有适销性或特
 * 定用途的隐含担保，详情请参照GPLv2许可协议。
 *
 * 您应已收到附随于本文件的GPLv2许可协议的副本，它通常在LICENSE.TXT文件中，如果
 * 没有，请查看：<http://www.gnu.org/licenses/>. 
 * ****************************************************************************/
 
/*
 * 队列设计方案：
 * 队列有“已用”和“未用”这两种空间，element_size保存队列成员占用空间大小，总成员个数
 * 由max_num表示，包含有效数据的成员个数由total_num表示。
 * 当删除一个队列成员后，total_num会减1，max_num不变，该队列成员占用的空间不会被释放，
 * 以备后续使用，而该队列成员会移动至队列末尾，成为“未用”空间。
 * 当往队列里添加数据时，如果total_num小于max_num，则使用“未用”空间里的队列成员来保存
 * 该数据。
 * member_type保存队列的成员数据类型：指针 或者 变量；如果队列主要用于存放指针，那么，
 * 在删除，销毁队列时，不会释放数据指针指向的内存空间；否则，会释放。
 * 部件的子部件队列的成员数据类型就是指针，主要用于引用主部件队列里的部件，销毁子部件队列
 * 时不会影响到主队列里的部件。
 * 
 * 队列的数据存储方式有两种：数组 和 链表；
 * 前者的读写速度较快，但数据插入效率较慢，因为在指定位置插入数据后，该位置后面的所有数
 * 据的位置需要向后移动，后面的数据元素越多，耗时越多。
 * 后者适合频繁的进行数据插入和删除，因为只需要找到目标位置的结点，然后插入新结点即可。
 * LCUI的文本位图层的处理就用到了后者，因为编辑文本主要是添加和删除文字。
 * */

//#define DEBUG
#include <LCUI_Build.h>
#include LC_LCUI_H
#include LC_MISC_H
#include LC_GRAPH_H
#include LC_WIDGET_H 
#include LC_ERROR_H
#include <unistd.h>

/************************ LCUI_Queue **********************************/
int Queue_Lock (LCUI_Queue *queue)
/* 功能：锁定一个队列，使之只能被一个线程使用 */
{ 
	return thread_mutex_lock(&queue->lock);
}

int Queue_UnLock (LCUI_Queue *queue)
/* 功能：解开队列锁 */
{
	return thread_mutex_unlock(&queue->lock);
}

int Queue_Using (LCUI_Queue * queue, int mode) 
/* 
 * 功能：设定队列的状态为“使用” 
 * 说明：参数mode需要是QUEUE_MODE_READ和QUEUE_MODE_WRITE这两种之一。
 * QUEUE_MODE_READ 表示“读”模式，由于是读，可以和其它以“读”模式访问本队列的线程共享。
 * QUEUE_MODE_WRITE 表示“写”模式，写只能由一个线程写，其它线程既不能读也不能写。
 * */
{ 
	if(mode == QUEUE_MODE_READ) {
	//	printf("use, queue: %p, mode: read\n", queue);
		return thread_rwlock_rdlock(&queue->lock); 
	} else {
		//printf("use, queue: %p, mode: write\n", queue);
		return thread_rwlock_wrlock(&queue->lock);  
	}
}

int Queue_End_Use (LCUI_Queue * queue) 
/* 功能：储存矩形数据的队列为空闲状态 */
{
	//switch(queue->lock.state) {
	    //case RWLOCK_WRITE:printf("end use, queue: %p, befor mode: write\n", queue);break;
	    //case RWLOCK_READ:printf("end use, queue: %p, befor mode: read\n", queue);break;
	    //case RWLOCK_FREE:printf("end use, queue: %p, befor mode: free\n", queue);break;
	//}
	return thread_rwlock_unlock(&queue->lock); 
}

void Queue_Init (LCUI_Queue * queue, size_t element_size, void (*func) ()) 
/* 功能：初始化队列 */
{
	thread_rwlock_init(&queue->lock);
	queue->member_type	= 0;
	queue->data_mode	= 0;
	queue->data_array	= NULL;
	queue->data_head_node.data = NULL;
	queue->data_head_node.prev = NULL;
	queue->data_head_node.next = NULL;
	queue->total_num	= 0;
	queue->max_num		= 0;
	queue->element_size	= element_size;
	queue->destroy_func	= func; 
}

void Queue_Using_Pointer(LCUI_Queue * queue)
/* 
 * 功能：设定队列成员类型为指针 
 * 说明：如果队列只是用于存放指针，并且不希望队列销毁后，指针指向的内存空间也被释放，可
 * 使用该函数设置。
 * */
{
	queue->member_type = 1;
}

int Queue_Get_Total(LCUI_Queue * queue)
/* 说明：获取队列当前总成员数量 */
{
	return queue->total_num;
}

int Queue_Set_DataMode(LCUI_Queue * queue, Queue_DataMode mode)
/* 
 * 功能：设定队列使用的数据储存模式
 * 说明：只能在初始化后且未加入成员时使用该函数
 * */
{
	if(queue->total_num > 0 || queue->max_num > 0) {
		return -1;
	}
	queue->data_mode = 1;
	return 0;
}


int Queue_Swap(LCUI_Queue * queue, int pos_a, int pos_b)
/* 功能：交换队列中指定位置两个成员的位置 */
{
	void *temp;
	
	if (pos_a >= queue->total_num || pos_b >= queue->total_num 
	 || pos_a == pos_b || queue->total_num <= 0) {
		return -1;
	}
	
	if(queue->data_mode == QUEUE_DATA_MODE_ARRAY) {
		temp = queue->data_array[pos_a];
		queue->data_array[pos_a] = queue->data_array[pos_b];
		queue->data_array[pos_b] = temp;
	} else {
		int i;
		LCUI_Node *a, *b;
		/* 找到两个位置的结点 */
		a = &queue->data_head_node;
		for(i=0; i<=pos_a; ++i) {
			a = a->next;
		}
		b = &queue->data_head_node;
		for(i=0; i<=pos_b; ++i) {
			b = b->next;
		}
		/* 交换指针 */
		temp = a->data;
		a->data = b->data;
		b->data = temp;
	}
	return 0;
}

void Destroy_Queue(LCUI_Queue * queue) 
/* 功能：释放队列占用的内存资源 */
{
	if(queue->member_type == 0) {
	/* 如果成员是普通类型，释放队列成员占用的内存空间 */ 
		while( Queue_Delete(queue, 0) );/* 清空队列成员 */
	} else {
		while( Queue_Delete_Pointer(queue, 0) );
	}
	if(queue->data_mode == QUEUE_DATA_MODE_ARRAY) {
		/* 释放二维指针占用的的内存空间 */ 
		free (queue->data_array);
	} else {
		LCUI_Node *p, *obj;
		p = queue->data_head_node.next;
		/* 切换到下个结点后，把上一个结点销毁 */
		while( p ) {
			obj = p;
			p = p->next;
			free( obj );
		}
	}
	queue->data_array = NULL;
	queue->data_head_node.next = NULL;
	queue->total_num = 0;
	queue->max_num = 0;
	thread_rwlock_destroy(&queue->lock);
}


void * Queue_Get (LCUI_Queue * queue, int pos)
/* 
 * 功能：从队列中获取指定位置的成员 
 * 说明：成功返回指向该成员的指针，失败返回NULL
 * 注意：请勿对返回的指针进行free操作
 * */
{
	void  *data = NULL;
	
	if( pos < 0) {
		return NULL;
	}
	if (queue->total_num > 0 && pos < queue->total_num) {
		if(queue->data_mode == QUEUE_DATA_MODE_ARRAY) {
			data = queue->data_array[pos]; 
		} else {
			int i;
			LCUI_Node *p;
			p = queue->data_head_node.next;
			for(i=0; i< pos && p->next; ++i) {
				p = p->next;
			}
			data = p->data;
		}
	}
	return data;
}

int Queue_Insert( LCUI_Queue * queue, int pos, const void *data)
/* 功能：向队列中指定位置插入成员 */
{
	int src_pos;
	src_pos = Queue_Add(queue, data);
	return Queue_Move(queue, pos, src_pos); 
}

int Queue_Insert_Pointer( LCUI_Queue * queue, int pos, const void *data)
/* 功能：向队列中指定位置插入成员的指针 */
{
	int src_pos;
	src_pos = Queue_Add_Pointer(queue, data);
	return Queue_Move(queue, pos, src_pos); 
}

int Queue_Move(LCUI_Queue *queue, int des_pos, int src_pos)
/* 功能：将队列中指定位置的成员移动至目的位置 */
{
	void *temp;
	int i, total;
	
	total = Queue_Get_Total(queue);
	if(des_pos < 0 || des_pos > total 
	|| src_pos < 0 || src_pos > total ) {
		return -1;
	}
	//printf("Queue_Move(): queue: %p, des pos: %d, src pos: %d, total: %d\n", 
	//queue, des_pos, src_pos, queue->total_num);
	if(des_pos == src_pos ) {
		return 0;
	}
	
	if(queue->data_mode == QUEUE_DATA_MODE_ARRAY) {
		temp = queue->data_array[src_pos];
		if (src_pos > des_pos) {
		/* 如果新位置在原位置的前面，把两位置之间的成员向右移动 */
			for (i = src_pos; i > des_pos; --i) {
				queue->data_array[i] = queue->data_array[i - 1];  
			}
		} else if (src_pos < des_pos) {
		/* 如果新位置在原位置的后面，把两位置之间的成员向左移动 */
			for (i = src_pos; i < des_pos; ++i) {
				queue->data_array[i] = queue->data_array[i + 1];  
			}
		}
		
		queue->data_array[des_pos] = temp;
	} else {
		LCUI_Node *temp, *p_src, *p_des;
		/* 得到源位置的结点的指针 */
		p_src = queue->data_head_node.next;
		for(i=0; p_src->next && i<src_pos; ++i ) {
			p_src = p_src->next;
		}
		/* 解除源结点与前后结点的链接 */
		temp = p_src->prev;
		temp->next = p_src->next;
		/* 若该结点不是在末尾 */
		if( p_src->next ) {
			p_src->next->prev = temp;
		}
		/* 得到目标位置的结点的指针 */
		p_des = queue->data_head_node.next;
		if(des_pos < src_pos) {
			for(i=0; p_des->next && i<des_pos; ++i ) {
				p_des = p_des->next;
			}
			/* 插入至目标结点的前面，并修改prev指针 */
			temp = p_des->prev;
			temp->next = p_src;
			p_src->prev = temp;
			/* 修改该结点的后结点 */
			p_src->next = p_des; 
			p_des->prev = p_src;
		} else {
			/* 数量有变动，目标位置在源位置后面，位置需向前移1个单位 */
			des_pos -= 1; 
			/* 目标位置的结点接前面 */
			for(i=0; p_des->next && i<des_pos; ++i ) {
				p_des = p_des->next;
			}
			temp = p_des->next;
			p_des->next = p_src;
			p_src->next = temp;
			p_src->prev = p_des;
		}
	}
	return 0;
}

int Queue_Quote( LCUI_Queue *des, LCUI_Queue *src )
/* 引用队列 */
{
	*des = *src;
	if( des->data_mode == QUEUE_DATA_MODE_ARRAY 
	&& src->data_mode == QUEUE_DATA_MODE_ARRAY ) {
		return 0;
	}
	else if( des->data_mode == QUEUE_DATA_MODE_LINKED_LIST
	 && src->data_mode == QUEUE_DATA_MODE_LINKED_LIST ) {
		/* 如果是数据储存方式是链表，那么，需要修改第一个结点的preve指针为当前头结点 */
		if( des->data_head_node.next ) {
			des->data_head_node.next->prev = &des->data_head_node;
		}
		return 0;
	}
	return -1;
}

int Queue_Replace_By_Flag(LCUI_Queue * queue, int pos, const void *data, int flag)
/* 功能：覆盖队列中指定位置的成员 */
{
	int i, total;
	total = Queue_Get_Total(queue);
	if(pos >= total) {	/* 如果超出队列范围 */
		return -1;
	}
	
	if(queue->data_mode == QUEUE_DATA_MODE_ARRAY) {
		/* 
		 * 考虑到队列成员有时会是结构体，并且结构体成员中可能会有指针，为了避免因重复覆盖不
		 * 对指针进行释放而导致的内存溢出，需要先调用析构函数对该成员进行销毁，因为析构函数
		 * 一般会对结构体中的指针进行释放，之后，再复制新成员的数据至该成员的内存空间。
		 *  */
		if( queue->destroy_func ) {
			queue->destroy_func(queue->data_array[pos]); 
		}
		if(flag == 1) {
			memcpy(queue->data_array[pos], data, queue->element_size);
		} else {
			/* 拷贝指针 */
			memcpy(&queue->data_array[pos], &data, sizeof(void*));
		}
	} else {
		LCUI_Node *p;
		
		p = queue->data_head_node.next;
		for(i=0; p->next && i<pos; ++i ) {
			p = p->next;
		}
		if(NULL != queue->destroy_func) {
			queue->destroy_func( p->data ); 
		}
		if(flag == 1) {
			memcpy(p->data, data, queue->element_size); 
		} else { 
			memcpy(&p->data, &data, sizeof(void*));
		}
	}
	return 0;
}

int Queue_Replace(LCUI_Queue * queue, int pos, const void *data)
/* 功能：覆盖队列中指定位置的成员内存空间里的数据 */
{
	return Queue_Replace_By_Flag(queue, pos, data, 1);
}

int Queue_Replace_Pointer(LCUI_Queue * queue, int pos, const void *data)
/* 功能：覆盖队列中指定位置的成员指针 */
{
	return Queue_Replace_By_Flag(queue, pos, data, 0);
}

static int Queue_Add_By_Flag(LCUI_Queue * queue, const void *data, int flag)
/* 
 * 功能：将新的成员添加至队列 
 * 说明：是否为新成员重新分配内存空间，由参数flag的值决定
 * 返回值：正常则返回在队列中的位置，错误则返回非0值
 * */
{
	int i, pos;
	size_t size; 
	LCUI_Node *p, *q;
	
	pos = queue->total_num;
	++queue->total_num;
	/* 如果数据是以数组形式储存 */
	if(queue->data_mode == QUEUE_DATA_MODE_ARRAY) {
		if(queue->total_num > queue->max_num) {
		/* 如果当前总数大于之前最大的总数 */
			queue->max_num = queue->total_num;
			size = sizeof(void*) * queue->total_num;
			/* 如果总数大于1，说明之前已经malloc过，直接realloc扩增内存 */
			if (queue->total_num > 1 && queue->data_array ) { 
				queue->data_array = (void **) 
					realloc( queue->data_array, size ); 
			} else {
				queue->data_array = (void **) malloc (sizeof(void*)); 
			}
			
			if( !queue->data_array ) {
				printf("Queue_Add_By_Flag(): "ERROR_MALLOC_ERROR);
				exit(-1);
			}
			if (flag == 1) {
				queue->data_array[pos] = malloc(queue->element_size);
			}
		}
		else if ( flag == 1 && !queue->data_array[pos] ) { 
			queue->data_array[pos] = 
					malloc(queue->element_size);
		}
		
		if(flag == 1) {
			memcpy(queue->data_array[pos], data, 
				queue->element_size);
		} else {
			memcpy(&queue->data_array[pos], &data, sizeof(void*));
		}
	} else {/* 否则，数据是以链表形式储存 */  
		DEBUG_MSG("new total_num: %d\n", queue->total_num);
		if(queue->total_num > queue->max_num) {
			p = &queue->data_head_node;
			DEBUG_MSG("head_node: %p, next: %p\n", p, p->next);
			for( i=0; i<pos && p->next; ++i ) {
				p = p->next;
			}
			q = (LCUI_Node*) malloc (sizeof(LCUI_Node)); 
			q->prev = p;
			q->next = NULL;
			p->next = q;
			p = q;
			queue->max_num = queue->total_num;
		} else {
			p = queue->data_head_node.next; 
			for(i=0; p->next && i<pos; ++i ) {
				p = p->next;
			} 
		}
		if(flag == 1) { 
			p->data = malloc ( queue->element_size );
			memcpy( p->data, data, queue->element_size );
		} else {
			memcpy( &p->data, &data, sizeof(void*) );
		}
	}
	return pos;
}

/* 打印队列信息，一般用于调试 */
void Print_Queue_Info( LCUI_Queue *queue )
{
	printf(
		"queue: %p, total: %d, max: %d, data_mode: %d\n"
		"data_array: %p, node: %p\n",
		queue, queue->total_num, queue->max_num, queue->data_mode,
		queue->data_array, queue->data_head_node.data
	);
}

int Queue_Add(LCUI_Queue * queue, const void *data) 
/* 
 * 功能：将新的成员添加至队列 
 * 说明：这个函数只是单纯的添加成员，如果想有更多的功能，需要自己实现
 * */
{
	return Queue_Add_By_Flag(queue, data, 1); 
}


int Queue_Add_Pointer(LCUI_Queue * queue, const void *data)
/* 
 * 功能：将新的成员添加至队列 
 * 说明：与Queue_Add函数不同，该函数只是修改指定位置的成员指针指向的地址，主要用
 * 与部件队列的处理上，有的部件需要从一个队列转移到另一个队列上，不重新分配内存空间，
 * 直接使用原来的内存地址，这是为了避免部件转移所在队列后，部件指针无效的问题。
 * */
{
	return Queue_Add_By_Flag(queue, data, 0); 
}

int Queue_Cat( LCUI_Queue *des, LCUI_Queue *src )
/* 功能：将一个队列拼接至另一个队列的末尾 */
{
	int i,total;
	
	if( !des || !src ) {
		return -1;
	}
	
	total = Queue_Get_Total( src );
	for( i=0; i<total; ++i ) {
		Queue_Add_Pointer( des, Queue_Get( src, i ) );
	}
	return 0;
}

int Queue_Empty(LCUI_Queue *queue)
/* 功能：检测队列是否为空 */
{
	if(queue->total_num > 0) {
		return 0;
	}
	
	return 1;
}

/* 查找指定成员指针所在队列中的位置 */
int Queue_Find( LCUI_Queue *queue, const void *p )
{
	void *tmp;
	int i, total; 
	
	total = Queue_Get_Total( queue );
	for(i=0; i<total; ++i) {
		tmp = Queue_Get( queue, i );
		if( tmp == p ) { 
			return i;
		}
	} 
	return -1;
}

static BOOL 
Queue_Delete_By_Flag(LCUI_Queue * queue, int pos, int flag) 
/* 
 * 功能：从队列中删除一个成员，并重新排列队列
 * 说明：处理方式因flag的值而不同 
 * 返回值：正常返回真（1），出错返回假（0）
 * */
{
	int i;
	void *save = NULL;
	
	/* 有效性检测 */
	if (pos >=0 && pos < queue->total_num && queue->total_num > 0);
	else {
		return FALSE;
	} 
	if(queue->data_mode == QUEUE_DATA_MODE_ARRAY) {
		save = queue->data_array[pos];/* 备份地址 */
		/* 移动排列各个成员位置，这只是交换指针的值，把需要删除的成员移至队列末尾 */
		for (i = pos; i < queue->total_num - 1; ++i) {
			queue->data_array[i] = queue->data_array[i + 1]; 
		}

		if(flag == 1) {
			queue->data_array[i] = save;
			memset(queue->data_array[i], 0, queue->element_size);
		} else {
			queue->data_array[i] = NULL;
		}
	} else {
		LCUI_Node *temp, *p_src, *p_des;
		/* 得到源位置的结点的指针 */
		p_src = queue->data_head_node.next;
		if( !p_src ) {
			return FALSE;
		}
		for(i=0; p_src->next && i<pos; ++i ) {
			p_src = p_src->next;
		} 
		/* 备份指针 */
		save = p_src->data;
		/* 如果后面还有结点 */
		if( p_src->next ) {
			/* 解除该位置的结点与前后结点的链接 */
			temp = p_src->prev;
			temp->next = p_src->next;
			p_src->next->prev = temp; 
			p_des = queue->data_head_node.next;
			if( p_des ) {
				/* 找到链表中最后一个结点 */
				while( p_des->next ) {
					p_des = p_des->next;
				}
				/* 把需删除的结点链接到链表尾部 */
				p_des->next = p_src;
				p_src->prev = p_des;
				p_src->next = NULL;
			}
		}
		if(flag == 1) { 
			memset(p_src->data, 0, queue->element_size);
		} else {
			p_src->data = NULL;
		}
	} 
	/* 
	 * 如果是使用本函数转移队列成员至另一个队列，该队列成员还是在同一个内存空间，
	 * 只不过，记录该成员的内存地址的队列不同。这种操作，本函数不会在源队列中保留
	 * 该成员的地址，因为源队列可能会被销毁，销毁时也会free掉队列中每个成员，而
	 * 目标队列未被销毁，且正在使用之前转移过来的成员，这会产生错误。
	 *  */
	--queue->total_num;
	
	if(flag == 1) { 
		/* 对该位置的成员进行析构处理 */
		if( queue->destroy_func ) {
			queue->destroy_func(save);
		} 
		/* 不需要释放内存，只有在调用Destroy_Queue函数时才全部释放 */
		//free(save); 
	}
	return TRUE;
}

int Queue_Delete (LCUI_Queue * queue, int pos)
/* 功能：从队列中删除一个成员，并释放该成员占用的内存资源 */
{
	return Queue_Delete_By_Flag(queue, pos, 1);
}

int Queue_Delete_Pointer (LCUI_Queue * queue, int pos) 
/* 功能：从队列中删除一个成员指针，不对该指针指向的内存进行释放 */
{
	return Queue_Delete_By_Flag(queue, pos, 0);
}

//#define _NEED_TEST_QUEUE_
#ifdef _NEED_TEST_QUEUE_
/*
 * 下面有几个main函数，用于对本文件内的函数进行测试，你可以选择其中一个main函数，编译
 * 并运行，看看结果
 * */
#define test_4
#ifdef test_1
/* 测试Queue_Cat函数 */
int main()
{
	int i, total;
	char ch, str[20];
	LCUI_Queue q1, q2;
	/* 初始化 */
	Queue_Init(&q1, sizeof(char), NULL);
	Queue_Init(&q2, sizeof(char), NULL);
	Queue_Set_DataMode(&q1, QUEUE_DATA_MODE_LINKED_LIST);
	Queue_Set_DataMode(&q2, QUEUE_DATA_MODE_LINKED_LIST);
	/* 添加0至9的字符至队列 */
	for(i=0; i<10; i++) {
		ch = '0' + i; 
		Queue_Add(&q1, &ch);
		Queue_Add(&q2, &ch);
	}
	total = Queue_Get_Total( &q1 );
	for(i=0; i<total; i++) {
		str[i] = *( (char*)Queue_Get(&q1, i) );  
	}
	str[i] = 0;
	
	printf("before, string:%s\n", str);
	Queue_Cat( &q1, &q2 ); /* 拼接队列 */
	total = Queue_Get_Total( &q1 );
	for(i=0; i<total; i++) {
		str[i] = *( (char*)Queue_Get(&q1, i) );  
	}
	str[i] = 0;
	printf("after, string:%s\n\n", str);
	
	Destroy_Queue(&q1);
	Destroy_Queue(&q2);
	return 0;
}
#endif
#ifdef test_2
/* 
 * 功能：测试通用队列的成员指针增删功能
 * 说明：先从队列1中获取指定位置的成员指针，之后删除该位置的成员指针，把成员指针添加至
 * 队列2中。
 *  */
int main()
{
	int i;
	char *p, ch, str[11];
	LCUI_Queue q1, q2;
	/* 初始化 */
	Queue_Init(&q1, sizeof(char), NULL);
	Queue_Init(&q2, sizeof(char), NULL);
	Queue_Set_DataMode(&q1, QUEUE_DATA_MODE_LINKED_LIST);
	/* 添加0至9的字符至队列 */
	for(i=0; i<10; i++) {
		ch = '0' + i; 
		Queue_Add(&q1, &ch);
	}
	/* 获取每个成员，并保存至字符串 */
	for(i=0; i<10; i++) {
		str[i] = *( (char*)Queue_Get(&q1, i) );  
	}
	str[i] = 0;
	
	printf("befor, string:%s\n", str);
	p = (char*)Queue_Get(&q1, 5);
	printf("delete char: %c\n", *p);
	Queue_Delete_Pointer(&q1, 5); 
	for(i=0; i<9; i++) {
		str[i] = *( (char*)Queue_Get(&q1, i) );
	}
	str[i] = 0;
	printf("after, string:%s\n\n", str);
	
	//Queue_Add_Pointer(&q2, p);
	Queue_Insert_Pointer( &q2, 0, p );
	printf("add char: %c\n", *p);
	for(i=0; i<Queue_Get_Total(&q2); i++) {
		str[i] = *( (char*)Queue_Get(&q2, i) );
	}
	str[i] = 0;
	printf("after, string:%s\n\n", str);
	
	int des_pos, src_pos;
	src_pos = 3;
	des_pos = 8;
	for(i=0; i<5; ++i) {
		p = (char*)Queue_Get(&q1, src_pos);
		printf("move char: %c, src pos: %d, des pos: %d\n", *p, src_pos, des_pos);
		Queue_Move(&q1, des_pos, src_pos);
	}
	for(i=0; i<Queue_Get_Total(&q1); i++) {
		str[i] = *( (char*)Queue_Get(&q1, i) );
	}
	str[i] = 0;
	printf("after, string:%s\n\n", str);
	
	
	Destroy_Queue(&q1);
	Destroy_Queue(&q2);
	return 0;
}
#endif
#ifdef test_3
/* 
 * 功能：测试通用队列的基本功能
 * 说明：此函数是将‘0’-’9‘的字符存入通用队列中，之后再取出保存至字符串中，最后打印字符串
 *  */
int main()
{
	int i;
	char ch, str[11];
	LCUI_Queue bq;
	/* 初始化 */
	Queue_Init(&bq, sizeof(char), NULL);
	/* 添加0至9的字符至队列 */
	for(i=0; i<10; i++) {
		ch = '0' + i;
		Queue_Add(&bq, &ch);
	}
	/* 获取每个成员，并保存至队列 */
	for(i=0; i<10; i++) {
		str[i] = *( (char*)Queue_Get(&bq, i) );  
	}
	str[i] = 0;
	
	printf("string:%s\n", str);
	
	Destroy_Queue(&bq);
	return 0;
}
#endif
#ifdef test_4
int main()
{
	clock_t start;
	int i, total;
	LCUI_Queue q1;
	/* 初始化 */
	Queue_Init(&q1, sizeof(int), NULL);
	//Queue_Set_DataMode(&q1, QUEUE_DATA_MODE_LINKED_LIST);
	nobuff_printf( "queue add......" );
	start = clock();
	/* 添加0至9的字符至队列 */
	for(i=0; i<20000; i++) {
		Queue_Add(&q1, &i);
	}
	nobuff_printf( "%ld us\n", clock()-start );
	total = Queue_Get_Total( &q1 );
	nobuff_printf( "queue get......" );
	start = clock();
	for(i=0; i<total; i++) {
		Queue_Get(&q1, i);  
	}
	nobuff_printf( "%ld us\n", clock()-start );
	Destroy_Queue(&q1);
	return 0;
}
#endif
#endif


/************************ LCUI_Queue End ******************************/

/************************* RectQueue **********************************/
void RectQueue_Init(LCUI_Queue *queue)
/* 功能：初始化储存矩形数据的队列 */
{
	/* 由于LCUI_Rect结构体中的成员没有指针，因此，不需要释放指针指向的内存，也就不需要析构函数 */
	Queue_Init(queue, sizeof(LCUI_Rect), NULL);
}

int RectQueue_Get( LCUI_Rect * rect, int pos, LCUI_Queue * queue)
/* 功能：从队列指定位置中获取一个矩形数据 */
{
	void *temp;
	temp = Queue_Get(queue, pos);
	if(NULL == temp) {
		return 0;
	}
	*rect = *((LCUI_Rect*)temp);
	return 1;
}

void Queue_Copy(LCUI_Queue *des, LCUI_Queue *src)
/* 功能：将源队列里的全部成员拷贝追加至目标队列里 */
{
	LCUI_Rect *rect;
	int i, total;
	total = Queue_Get_Total(src);
	for(i=0; i<total; ++i) {
		rect = (LCUI_Rect *)Queue_Get(src, i);/* 获取源队列里的成员 */
		//printf("[%d] rect: %d,%d, %d,%d\n", i, rect->x, rect->y, rect->width, rect->height);
		RectQueue_Add(des, *rect); /* 添加至目标队列里 */
	}
}
 
int RectQueue_Add (LCUI_Queue * queue, LCUI_Rect rect) 
/* 功能：将矩形数据追加至队列 */
{ 
	int i, flag = 0;
	LCUI_Rect t_rect; 
	LCUI_Queue rect_buff;
	
	//if(debug_mark)
	//	printf("New : [%d,%d] %d,%d\n", rect.x, rect.y, rect.width, rect.height);
	
	if(!Rect_Valid(rect)) {
		//printf("not valid\n");
		return -1;
	}
	
	RectQueue_Init(&rect_buff);
	
	for (i = 0; i < queue->total_num; ++i) {
		if(RectQueue_Get(&t_rect, i, queue)) {
			//if(debug_mark)
			//  printf("temp : [%d,%d] %d,%d\n", t_rect.x, t_rect.y, t_rect.width, t_rect.height);
			
			if (!Rect_Valid(t_rect)) {
			/* 删除这个矩形数据，因为它是无效的 */
				Queue_Delete (queue, i); 
			} else if (Rect_Include_Rect (rect, t_rect)) {
			/* 删除这个矩形数据，因为它已经被新增的矩形区域包含 */
				Queue_Delete (queue, i); 
			} else if (Rect_Include_Rect (t_rect, rect)) {
			/* 如果新增的矩形数据与已存在的矩形数据属于包含关系 */
				//if(debug_mark) 
				//  printf("Include 2\n");
				  
				flag = 1;
				break;
			} else if(Rect_Equal(rect, t_rect)) {
			/* 相等的就不需要了 */
				//if(debug_mark)
				//  printf("Equal || not valid\n");
				
				flag = 1;
				break;
			} else if(Rect_Is_Overlay(rect, t_rect)) {
				/* 如果新增的矩形与队列中的矩形重叠 */ 
				/* 将矩形分离成若干个不重叠的矩形，之后将它们添加进去 */
				//printf("Rect_Is_Overlay(rect, t_rect)\n");
				Cut_Overlay_Rect(t_rect, rect, &rect_buff);
				//debug_mark = 1;
				Queue_Copy(queue, &rect_buff);
				//debug_mark = 0;
				flag = 1;
				break;
			}
		} else {
			break;
		}
	}
	
	Destroy_Queue(&rect_buff);
	if (flag == 0) { /* 没有的话，就需要添加至队列 */ 
		return Queue_Add(queue, &rect);
	}
	/* 销毁队列 */
	//if(debug_mark) 
	//	printf("done\n");
	return -1;
}
/************************* RectQueue end *******************************/

