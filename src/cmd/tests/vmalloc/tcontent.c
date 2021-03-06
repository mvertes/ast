/***********************************************************************
*                                                                      *
*               This software is part of the ast package               *
*          Copyright (c) 1999-2013 AT&T Intellectual Property          *
*                      and is licensed under the                       *
*                 Eclipse Public License, Version 1.0                  *
*                    by AT&T Intellectual Property                     *
*                                                                      *
*                A copy of the License is available at                 *
*          http://www.eclipse.org/org/documents/epl-v10.html           *
*         (with md5 checksum b35adb5213ca9657e911e9befb180842)         *
*                                                                      *
*              Information and Software Systems Research               *
*                            AT&T Research                             *
*                           Florham Park NJ                            *
*                                                                      *
*               Glenn Fowler <glenn.s.fowler@gmail.com>                *
*                                                                      *
***********************************************************************/
#include	"vmtest.h"
#include	<pthread.h>

#define N_EMPHE		100000		/* max #allocs for emphemeral threads	*/

#define	CH_FREE		1		/* character to fill free space		*/
#define	CH_MALLOC	3		/* character to fill malloc space	*/
#define	CH_REALLOC	5		/* character to fill realloc space	*/

static size_t		Nthread = N_THREAD;/* number of main threads		*/
static size_t		Nalloc = 10000;	/* total number of allocations	*/
static size_t		Life = 10;	/* life before free or realloc	*/

static size_t		Smalllo = 10;
static size_t		Smallhi = 100;
static size_t		Smalllf = 10;

static size_t		Largelo = 4000;	/* low end of size range		*/
static size_t		Largehi = 8000; /* high end of size range		*/
static size_t		Largelf = 1000;	/* life of an allocated piece		*/

static size_t		Maxbusy = 0;	/* size of max busy space at any time	*/
static size_t		Curbusy = 0;	/* size of busy space at current time	*/

/* define alignment requirement */
typedef union type_u
{	int		i, *ip;
	short		s, *sp;
	long		l, *lp;
	long long	ll, *llp;
	float		f, *fp;
	double		d, *dp;
	long double	dd, *ddp;
} Type_t;

typedef struct _align_s
{	char	c;
	Type_t	type;
} Align_t;

#define ALIGNMENT	(sizeof(Align_t) - sizeof(Type_t))

typedef struct _piece_s	Piece_t;
struct _piece_s
{	Piece_t*	next;
	Piece_t*	free;
	char*		data;
	size_t		size;
};

typedef struct _thread_s
{	size_t		nalloc;
	Piece_t*	list;
} Tdata_t;

Tdata_t	Tdata[N_THREAD];

void* simulate(void* arg)
{
	int		k, p, a;
	unsigned int	rand;
	size_t		sz, nalloc, lf;
	Piece_t		*list, *up, *next;
	void		*data;
	int		thread = (int)((long)arg);
	pthread_t	ethread;
	Tdata_t		*tdata = &Tdata[thread];
	int		warn_align = 1;
	unsigned int	Rand = 1001; /* use a local RNG so that threads work uniformly */
#define RANDOM()	(Rand = Rand*9999991 + 991)
extern int N_resize, N_alloc, N_free;

	list = tdata->list;
	nalloc = tdata->nalloc;
	for(k = 0; k < nalloc; ++k)
	{	
		/* update all that needs updating */
		for(up = list[k].free; up; up = next)
		{	next = up->next;

			if((rand = RANDOM()%100) < 95)
			{	
				memset(up->data, CH_FREE, up->size);
				free(up->data);
				asosubsize(&Curbusy, up->size);
				up->data = NULL;
				up->size = 0;
			}
			else 
			{	sz = RANDOM()%(2*up->size) + 1;
				if(!(up->data = realloc(up->data, sz)) )
					terror("Thread %d: failed to realloc(org=%d sz=%d)", up->size, sz);
				if((a = (unsigned long)(list[k].data) % ALIGNMENT) != 0)
					terror("Thread %d: block=%#0x mod %d == %d",
						thread, list[k].data, ALIGNMENT, a);

				asosubsize(&Curbusy, up->size);
				asoaddsize(&Curbusy, sz);
				asomaxsize(&Maxbusy, Curbusy);
				up->size = sz;
				memset(up->data, CH_REALLOC, sz);

				/* get a random point in the future to update */
				if((p = k+1 + RANDOM()%Life) < nalloc)
				{	up->next = list[p].free;
					list[p].free = up;
				}
			}
		}

		/* get a random size in given range */
		if(RANDOM()%100 < 95)
		{	sz = RANDOM()%(Smallhi-Smalllo+1) + Smalllo;
			lf = Smalllf;
		}
		else
		{	sz = RANDOM()%(Largehi-Largelo+1) + Largelo;
			lf = Largelf;
		}

		if(!(list[k].data = malloc(sz)) )
			terror("Thread %d: failed to malloc(%d)", thread, sz);
		if((a = (unsigned long)(list[k].data) % ALIGNMENT) != 0)
		{	if(warn_align)
				tinfo("Thread %d: block=%#0x mod %d == %d", thread, list[k].data, ALIGNMENT, a);
			warn_align = 0;
		}
			
		asoaddsize(&Curbusy, sz);
		asomaxsize(&Maxbusy, Curbusy);
		list[k].size = sz;
		memset(list[k].data, CH_MALLOC, sz);

		/* set time to update */
		if((p = k+1 + lf - RANDOM()%(lf/5)) < nalloc )
		{	list[k].next = list[p].free;
			list[p].free = &list[k];
		}
	}

	return (void*)0;
}

tmain()
{
	int		i, k, t, ch, arg1, arg2, arg3, nalloc, rv;
	size_t		sz;
	void		*status;
	pthread_t	th[N_THREAD];

	for(; argc > 1; --argc, ++argv)
	{	if(argv[1][0] != '-')
			continue;
		else if(argv[1][1] == 'a')
			Nalloc = atoi(argv[1]+2);
		else if(argv[1][1] == 'l')
			Life = atoi(argv[1]+2);
		else if(argv[1][1] == 't')
			Nthread = atoi(argv[1]+2);
		else if(argv[1][1] == 'z')
		{	sscanf(argv[1]+2, "%d,%d,%d", &arg1, &arg2, &arg3);
			Smalllo = arg1;
			Smallhi = arg2;
			Smalllf = arg3;
		}
		else if(argv[1][1] == 'Z')
		{	sscanf(argv[1]+2, "%d,%d,%d", &arg1, &arg2, &arg3);
			Largelo = arg1;
			Largehi = arg2;
			Largelf = arg3;
		}
	}

	if(Nthread <= 0 || Nthread > N_THREAD)
		terror("nthreads=%d must be in 1..%d", Nthread, N_THREAD);

	tresource(-1, 0);

	nalloc = Nalloc/Nthread; /* #of allocations per thread */
	Nalloc = nalloc*Nthread;

	for(i = 0; i < Nthread; ++i)
	{	Tdata[i].nalloc = nalloc;
		sz = Tdata[i].nalloc*sizeof(Piece_t);
		if(!(Tdata[i].list = (Piece_t*)malloc(sz)) )
			terror("Failed allocating list of objects nalloc=%d", Tdata[i].nalloc);
		memset(Tdata[i].list, 0, Tdata[i].nalloc*sizeof(Piece_t));
		Curbusy += sz;
	}
	Maxbusy = Curbusy;

	tinfo("Nstep=%d Nthread=%d Small(min=%d,max=%d,life=%d) Large(min=%d,max=%d,life=%d) ",
		Nalloc, Nthread, Smalllo, Smallhi, Smalllf, Largelo, Largehi, Largelf);

	for(i = 0; i < Nthread; ++i)
	{	if((rv = pthread_create(&th[i], NULL, simulate, (void*)((long)i))) != 0 )
			terror("Failed to create simulation thread %d", i);
	}

	for(i = 0; i < Nthread; ++i)
	{	if((rv = pthread_join(th[i], &status)) != 0 )
			terror("Failed waiting for simulation thread %d", i);
	}

	for(i = 0; i < Nthread; ++i)
	{	for(k = 0; k < Tdata[i].nalloc; ++k)
		{	if(Tdata[i].list[k].size == 0)
				continue;
			if((ch = Tdata[i].list[k].data[0]) == CH_MALLOC || ch == CH_REALLOC)
			{	for(t = 1; t < Tdata[i].list[k].size; ++t)
					if(Tdata[i].list[k].data[t] != ch)
						terror("Corrupted allocated data");
			}
			else	terror("Unknown allocated data");
		}
	}

	tinfo("Curbusy=%ld Maxbusy=%ld", Curbusy, Maxbusy);

	tresource(0, Maxbusy);

#if VMALLOC
{	Vmstat_t	vmst;
	vmstat(Vmregion, &vmst);
	twarn(vmst.mesg);
}
#endif

	texit(0);
}
