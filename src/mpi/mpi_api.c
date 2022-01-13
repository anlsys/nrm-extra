/*******************************************************************************
 * Copyright 2019 UChicago Argonne, LLC.
 * (c.f. AUTHORS, LICENSE)
 *
 * This file is part of the nrm-extra project.
 * For more info, see https://github.com/anlsys/nrm-extra
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *******************************************************************************/

/* Filename: mpi_api.c
 *
 * Description: This Message Passing Interface(MPI) libary allows
 * application of runtime policies for energy efficiency through the MPI
 * standard profiling interface(PMPI).
 *
 * The current implementation passes phase contextual information (compute
 * time and barrier time) to the Argo Node Resource Manager(NRM).
 * The NRM using this information invokes power policies to improve energy
 * efficiency of the node.
 */

#define _GNU_SOURCE
#include <assert.h>
#include <ctype.h>
#include <dlfcn.h>
#include <mpi.h>
#include <sched.h> // sched_getcpu
#include <stdio.h> // printf
#include <stdlib.h> // exit, atoi
#include <time.h>

#include "nrm.h"

#include "nrm_mpi.h"

static struct nrm_context *ctxt;
static struct nrm_scope *scope;

NRM_MPI_DECL(MPI_Allreduce,
             int,
             const void *sendbuf,
             void *recvbuf,
             int count,
             MPI_Datatype datatype,
             MPI_Op op,
             MPI_Comm comm)
{
	NRM_MPI_RESOLVE(MPI_Allreduce);
	nrm_send_progress(ctxt, 1, scope);

	int ret = NRM_MPI_REALNAME(MPI_Allreduce, sendbuf, recvbuf, count,
	                           datatype, op, comm);
	nrm_send_progress(ctxt, 1, scope);
	return ret;
}

NRM_MPI_DECL(MPI_Barrier, int, MPI_Comm comm)
{
	NRM_MPI_RESOLVE(MPI_Barrier);
	nrm_send_progress(ctxt, 1, scope);

	int ret = NRM_MPI_REALNAME(MPI_Barrier, comm);
	nrm_send_progress(ctxt, 1, scope);

	return ret;
}

NRM_MPI_DECL(MPI_Comm_size, int, MPI_Comm comm, int *size)
{
	NRM_MPI_RESOLVE(MPI_Comm_size);
	return NRM_MPI_REALNAME(MPI_Comm_size, comm, size);
}

NRM_MPI_DECL(MPI_Comm_rank, int, MPI_Comm comm, int *rank)
{
	NRM_MPI_RESOLVE(MPI_Comm_rank);
	return NRM_MPI_REALNAME(MPI_Comm_rank, comm, rank);
}

NRM_MPI_DECL(MPI_Finalize, int, void)
{
	NRM_MPI_RESOLVE(MPI_Finalize);
	nrm_fini(ctxt);
	nrm_scope_delete(scope);
	nrm_ctxt_delete(ctxt);
	return NRM_MPI_REALNAME(MPI_Finalize);
}

NRM_MPI_DECL(MPI_Init, int, int *argc, char ***argv)
{

	int ret, rank, cpu;

	NRM_MPI_RESOLVE(MPI_Init);
	ret = NRM_MPI_REALNAME(MPI_Init, argc, argv);
	cpu = sched_getcpu();

	NRM_MPI_INNER_NAME(MPI_Comm_rank, MPI_COMM_WORLD, &rank);

	ctxt = nrm_ctxt_create();
	nrm_init(ctxt, "nrm-pmpi", rank, cpu);
	scope = nrm_scope_create();
	nrm_scope_threadshared(scope);
	return ret;
}
