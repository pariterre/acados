/*
 *    This file is part of acados.
 *
 *    acados is free software; you can redistribute it and/or
 *    modify it under the terms of the GNU Lesser General Public
 *    License as published by the Free Software Foundation; either
 *    version 3 of the License, or (at your option) any later version.
 *
 *    acados is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *    Lesser General Public License for more details.
 *
 *    You should have received a copy of the GNU Lesser General Public
 *    License along with acados; if not, write to the Free Software Foundation,
 *    Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include "acados/ocp_nlp/ocp_nlp_gn_sqp.h"

// external
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
// blasfeo
#include "blasfeo/include/blasfeo_target.h"
#include "blasfeo/include/blasfeo_common.h"
#include "blasfeo/include/blasfeo_d_aux.h"
#include "blasfeo/include/blasfeo_d_aux_ext_dep.h"
#include "blasfeo/include/blasfeo_d_blas.h"
// acados
#include "acados/ocp_qp/ocp_qp_common.h"
#include "acados/ocp_nlp/ocp_nlp_common.h"
#include "acados/sim/sim_casadi_wrapper.h"
#include "acados/sim/sim_common.h"
#include "acados/sim/sim_collocation.h"
#include "acados/utils/print.h"
#include "acados/utils/timing.h"
#include "acados/utils/types.h"
#include "acados/utils/mem.h"

static int get_max_sim_workspace_size(ocp_nlp_dims *dims, ocp_nlp_gn_sqp_args *args)
{
    sim_dims sim_dims;
    int sim_work_size;

    int max_sim_work_size = 0;

    for (int ii = 0; ii < dims->N; ii++)
    {
        cast_nlp_dims_to_sim_dims(&sim_dims, dims, ii);
        // sim_in_size = sim_in_calculate_size(&sim_dims);
        // if (sim_in_size > *max_sim_in_size) *max_sim_in_size = sim_in_size;
        // sim_out_size = sim_out_calculate_size(&sim_dims);
        // if (sim_out_size > *max_sim_out_size) *max_sim_out_size = sim_out_size;
        sim_work_size = args->sim_solvers[ii]->calculate_workspace_size(&sim_dims, args->sim_solvers_args[ii]);
        if (sim_work_size > max_sim_work_size) max_sim_work_size = sim_work_size;
    }
    return max_sim_work_size;
}


int ocp_nlp_gn_sqp_calculate_args_size(ocp_nlp_dims *dims, ocp_qp_xcond_solver_fcn_ptrs *qp_solver, sim_solver_fcn_ptrs *sim_solvers)
{
    int size = 0;

    ocp_qp_dims qp_dims;
    cast_nlp_dims_to_qp_dims(&qp_dims, dims);
    size += sizeof(ocp_nlp_gn_sqp_args);
    size += sizeof(ocp_qp_xcond_solver_fcn_ptrs);

    size += qp_solver->calculate_args_size(&qp_dims, qp_solver->qp_solver);

    sim_dims sim_dims;

    size += dims->N*sizeof(sim_solver_fcn_ptrs *);
    size += dims->N*sizeof(void *);  //sim_solvers_args

    for (int ii = 0; ii < dims->N; ii++)
    {
        cast_nlp_dims_to_sim_dims(&sim_dims, dims, ii);
        size += sizeof(sim_solver_fcn_ptrs);
        size += sim_solvers[ii].calculate_args_size(&sim_dims);
    }

    return size;
}



ocp_nlp_gn_sqp_args *ocp_nlp_gn_sqp_assign_args(ocp_nlp_dims *dims, ocp_qp_xcond_solver_fcn_ptrs *qp_solver, sim_solver_fcn_ptrs *sim_solvers, void *raw_memory)
{
    ocp_nlp_gn_sqp_args *args;

    ocp_qp_dims qp_dims;
    cast_nlp_dims_to_qp_dims(&qp_dims, dims);

    char *c_ptr = (char *) raw_memory;

    args = (ocp_nlp_gn_sqp_args *) c_ptr;
    c_ptr += sizeof(ocp_nlp_gn_sqp_args);

    args->qp_solver = (ocp_qp_xcond_solver_fcn_ptrs*) c_ptr;
    c_ptr += sizeof(ocp_qp_xcond_solver_fcn_ptrs);

    // copy function pointers
    *args->qp_solver = *qp_solver;

    args->qp_solver_args = args->qp_solver->assign_args(&qp_dims, qp_solver->qp_solver, c_ptr);
    c_ptr += args->qp_solver->calculate_args_size(&qp_dims, qp_solver->qp_solver);

    sim_dims sim_dims;

    args->sim_solvers = (sim_solver_fcn_ptrs **) c_ptr;
    c_ptr += dims->N*sizeof(sim_solver_fcn_ptrs *);

    args->sim_solvers_args = (void **) c_ptr;
    c_ptr += dims->N*sizeof(void *);

    for (int ii = 0; ii < dims->N; ii++)
    {
        cast_nlp_dims_to_sim_dims(&sim_dims, dims, ii);

        args->sim_solvers[ii] = (sim_solver_fcn_ptrs *) c_ptr;
        c_ptr += sizeof(sim_solver_fcn_ptrs);

        // copy function pointers
        *args->sim_solvers[ii] = sim_solvers[ii];

        args->sim_solvers_args[ii] = args->sim_solvers[ii]->assign_args(&sim_dims, c_ptr);
        c_ptr += args->sim_solvers[ii]->calculate_args_size(&sim_dims);
    }

    assert((char*)raw_memory + ocp_nlp_gn_sqp_calculate_args_size(dims, qp_solver, sim_solvers) == c_ptr);

    return args;
}



int ocp_nlp_gn_sqp_calculate_memory_size(ocp_nlp_dims *dims, ocp_nlp_gn_sqp_args *args)
{
    int size = 0;

    ocp_qp_dims qp_dims;
    cast_nlp_dims_to_qp_dims(&qp_dims, dims);

    size+= sizeof(ocp_nlp_gn_sqp_memory);

    int N = dims->N;

    size += sizeof(double *) * (N + 1);  // x
    size += sizeof(double *) * (N + 1);  // u
    size += sizeof(double *) * (N + 1);  // lam
    size += sizeof(double *) * N;  // pi

    for (int ii = 0; ii <= N; ii++)
    {
        size += sizeof(double)*dims->nx[ii];  // x
        size += sizeof(double)*dims->nu[ii];  // u
        size += sizeof(double)*2*(dims->nb[ii] + dims->ng[ii] + dims->nh[ii]);  // lam
        if (ii < N)
        {
            size += sizeof(double)*dims->nx[ii+1];  // pi
        }
    }

    size += args->qp_solver->calculate_memory_size(&qp_dims, args->qp_solver_args);

    sim_dims sim_dims;

    size += N*sizeof(void *);  // sim_solvers_mem

    for (int ii = 0; ii < N; ii++)
    {
        cast_nlp_dims_to_sim_dims(&sim_dims, dims, ii);
        size += args->sim_solvers[ii]->calculate_memory_size(&sim_dims, args->sim_solvers_args[ii]);
    }

    return size;
}



ocp_nlp_gn_sqp_memory *ocp_nlp_gn_sqp_assign_memory(ocp_nlp_dims *dims, ocp_nlp_gn_sqp_args *args, void *raw_memory)
{
    char *c_ptr = (char *) raw_memory;

    ocp_qp_dims qp_dims;
    cast_nlp_dims_to_qp_dims(&qp_dims, dims);

    ocp_nlp_gn_sqp_memory *mem = (ocp_nlp_gn_sqp_memory *)c_ptr;
    c_ptr += sizeof(ocp_nlp_gn_sqp_memory);

    assert((size_t)c_ptr % 8 == 0 && "memory not 8-byte aligned!");

    int N = dims->N;

    mem->num_vars = number_of_primal_vars(dims);

    // double pointers
    assign_double_ptrs(N+1, &mem->x, &c_ptr);
    assign_double_ptrs(N+1, &mem->u, &c_ptr);
    assign_double_ptrs(N, &mem->pi, &c_ptr);
    assign_double_ptrs(N+1, &mem->lam, &c_ptr);

    // doubles
    assert((size_t)c_ptr % 8 == 0 && "memory not 8-byte aligned!");

    for (int ii = 0; ii <= N; ii++)
    {
        assign_double(dims->nx[ii], &mem->x[ii], &c_ptr);
        assign_double(dims->nu[ii], &mem->u[ii], &c_ptr);
        if (ii < N)
        {
            assign_double(dims->nx[ii+1], &mem->pi[ii], &c_ptr);
        }
        assign_double(2*(dims->nb[ii] + dims->ng[ii] + dims->nh[ii]), &mem->lam[ii], &c_ptr);
    }

    assert((size_t)c_ptr % 8 == 0 && "memory not 8-byte aligned!");

    // QP solver
    mem->qp_solver_mem = args->qp_solver->assign_memory(&qp_dims, args->qp_solver_args, c_ptr);
    c_ptr += args->qp_solver->calculate_memory_size(&qp_dims, args->qp_solver_args);

    // integrators
    sim_dims sim_dims;

    mem->sim_solvers_mem = (void **) c_ptr;
    c_ptr += N*sizeof(void *);

    for (int ii = 0; ii < N; ii++)
    {
        cast_nlp_dims_to_sim_dims(&sim_dims, dims, ii);
        mem->sim_solvers_mem[ii] = args->sim_solvers[ii]->assign_memory(&sim_dims, args->sim_solvers_args[ii], c_ptr);
        c_ptr += args->sim_solvers[ii]->calculate_memory_size(&sim_dims, args->sim_solvers_args[ii]);
    }

    mem->dims = dims;

    assert((char *)raw_memory + ocp_nlp_gn_sqp_calculate_memory_size(dims, args) == c_ptr);

    return mem;
}



int ocp_nlp_gn_sqp_calculate_workspace_size(ocp_nlp_dims *dims, ocp_nlp_gn_sqp_args *args)
{
	// loop index
	int ii;

	// extract dims
	int N = dims->N;
	int *nx = dims->nx;
	int *nu = dims->nu;
	int *nb = dims->nb;
	int *ng = dims->ng;
	int *ns = dims->ns;

    int size = 0;

    ocp_qp_dims qp_dims;
    cast_nlp_dims_to_qp_dims(&qp_dims, dims);

    size += sizeof(ocp_nlp_gn_sqp_work);

//    size += number_of_primal_vars(dims) * sizeof(double);  // w

    size += ocp_qp_in_calculate_size(&qp_dims);
    size += ocp_qp_out_calculate_size(&qp_dims);
    size += args->qp_solver->calculate_workspace_size(&qp_dims, args->qp_solver_args);

    sim_dims sim_dims;

    size += N*sizeof(sim_in *);
    size += N*sizeof(sim_out *);
    size += N*sizeof(void *);  // sim_work

    size += get_max_sim_workspace_size(dims, args);

    for (ii = 0; ii < N; ii++)
    {
        cast_nlp_dims_to_sim_dims(&sim_dims, dims, ii);
        size += sim_in_calculate_size(&sim_dims);
        size += sim_out_calculate_size(&sim_dims);
    }


    size += 2*(N+1)*sizeof(struct blasfeo_dvec); // tmp_nux tmp_nbg

    for (ii = 0; ii < N+1; ii++)
    {
        size += blasfeo_memsize_dvec(nx[ii] + nu[ii]);
        size += blasfeo_memsize_dvec(nb[ii] + ng[ii]);
    }

    make_int_multiple_of(64, &size);
    size += 1 * 64;

    return size;
}



void ocp_nlp_gn_sqp_cast_workspace(ocp_nlp_gn_sqp_work *work, ocp_nlp_gn_sqp_memory *mem, ocp_nlp_gn_sqp_args *args)
{
    char *c_ptr = (char *)work;
    c_ptr += sizeof(ocp_nlp_gn_sqp_work);

	// extract dims
    int N = mem->dims->N;
    int *nx = mem->dims->nx;
    int *nu = mem->dims->nu;
    int *nb = mem->dims->nb;
    int *ng = mem->dims->ng;

    ocp_qp_dims qp_dims;
    cast_nlp_dims_to_qp_dims(&qp_dims, mem->dims);

    // set up common nlp workspace
//    assign_double(mem->num_vars, &work->w, &c_ptr);

    // set up local SQP data
    assign_blasfeo_dvec_structs(N+1, &work->tmp_nux, &c_ptr);
    assign_blasfeo_dvec_structs(N+1, &work->tmp_nbg, &c_ptr);

    align_char_to(64, &c_ptr);

	// tmp_nux
    for (int ii = 0; ii <= N; ii++)
    {
        assign_blasfeo_dvec_mem(nx[ii]+nu[ii], &work->tmp_nux[ii], &c_ptr);
    }
	// tmp_nbg
    for (int ii = 0; ii <= N; ii++)
    {
        assign_blasfeo_dvec_mem(nb[ii]+ng[ii], &work->tmp_nbg[ii], &c_ptr);
    }

    // set up QP solver
    work->qp_in = assign_ocp_qp_in(&qp_dims, c_ptr);
    c_ptr += ocp_qp_in_calculate_size(&qp_dims);
    work->qp_out = assign_ocp_qp_out(&qp_dims, c_ptr);
    c_ptr += ocp_qp_out_calculate_size(&qp_dims);

    work->qp_work = (void *)c_ptr;
    c_ptr += args->qp_solver->calculate_workspace_size(&qp_dims, args->qp_solver_args);

    // set up integrators
    sim_dims sim_dims;

    work->sim_in = (sim_in **) c_ptr;
    c_ptr += mem->dims->N*sizeof(sim_in *);
    work->sim_out = (sim_out **) c_ptr;
    c_ptr += mem->dims->N*sizeof(sim_out *);
    work->sim_solvers_work = (void **) c_ptr;
    c_ptr += mem->dims->N*sizeof(void *);

    int max_sim_work_size = get_max_sim_workspace_size(mem->dims, args);

    work->sim_solvers_work[0] = (void *)c_ptr;
    c_ptr += max_sim_work_size;

    for (int ii = 0; ii < mem->dims->N; ii++)
    {
        cast_nlp_dims_to_sim_dims(&sim_dims, mem->dims, ii);

        work->sim_in[ii] = assign_sim_in(&sim_dims, c_ptr);
        c_ptr += sim_in_calculate_size(&sim_dims);
        work->sim_out[ii] = assign_sim_out(&sim_dims, c_ptr);
        c_ptr += sim_out_calculate_size(&sim_dims);

        if (ii > 0) work->sim_solvers_work[ii] = work->sim_solvers_work[0];
    }

    assert((char *)work + ocp_nlp_gn_sqp_calculate_workspace_size(mem->dims, args) >= c_ptr);
}



static void initialize_objective(const ocp_nlp_in *nlp_in, ocp_nlp_gn_sqp_args *args, ocp_nlp_gn_sqp_memory *gn_sqp_mem, ocp_nlp_gn_sqp_work *work)
{
    int N = nlp_in->dims->N;
    int *nx = nlp_in->dims->nx;
    int *nu = nlp_in->dims->nu;
    ocp_nlp_ls_cost *cost = (ocp_nlp_ls_cost*) nlp_in->cost;

	struct blasfeo_dmat *sRSQrq = work->qp_in->RSQrq;

    // TODO(rien): only for least squares cost with state and control reference atm
    for (int_t i = 0; i <= N; i++)
	{

        // copy R
        blasfeo_pack_dmat(nu[i], nu[i], &cost->W[i][nx[i]*(nx[i]+nu[i])+nx[i]], nx[i] + nu[i], &sRSQrq[i], 0, 0);
        // copy Q
        blasfeo_pack_dmat(nx[i], nx[i], &cost->W[i][0], nx[i] + nu[i], &sRSQrq[i], nu[i], nu[i]);
        // copy S
        blasfeo_pack_tran_dmat(nu[i], nx[i], &cost->W[i][nx[i]], nx[i] + nu[i], &sRSQrq[i], nu[i], 0);

    }
}



static void initialize_trajectories(const ocp_nlp_out *nlp_out, ocp_nlp_gn_sqp_memory *gn_sqp_mem,
    ocp_nlp_gn_sqp_work *work)
{

    int N = nlp_out->dims->N;
    int *nx = nlp_out->dims->nx;
    int *nu = nlp_out->dims->nu;
//    real_t *w = work->w;

	int ii;
	for (ii=0; ii<=N; ii++)
	{
		blasfeo_pack_dvec(nu[ii], gn_sqp_mem->u[ii], nlp_out->ux+ii, 0);
		blasfeo_pack_dvec(nx[ii], gn_sqp_mem->x[ii], nlp_out->ux+ii, nu[ii]);
	}
	return;
}



static void multiple_shooting(ocp_nlp_in *nlp_in, ocp_nlp_out *nlp_out, ocp_nlp_gn_sqp_args *args, ocp_nlp_gn_sqp_memory *mem, ocp_nlp_gn_sqp_work *work)
{

	// loop index
	int i;

	// extract dims
    int N = nlp_in->dims->N;
    int *nx = nlp_in->dims->nx;
    int *nu = nlp_in->dims->nu;
    int *nb = nlp_in->dims->nb;
    int *ng = nlp_in->dims->ng;

//    real_t *w = work->w;
    struct blasfeo_dvec *tmp_nux = work->tmp_nux;
    struct blasfeo_dvec *tmp_nbg = work->tmp_nbg;

    ocp_nlp_ls_cost *cost = (ocp_nlp_ls_cost *) nlp_in->cost;
    real_t **y_ref = cost->y_ref;

    struct blasfeo_dmat *sBAbt = work->qp_in->BAbt;
    struct blasfeo_dvec *sb = work->qp_in->b;
    struct blasfeo_dmat *sRSQrq = work->qp_in->RSQrq;
    struct blasfeo_dvec *srq = work->qp_in->rq;
    struct blasfeo_dvec *sd = work->qp_in->d;

    for (i = 0; i < N; i++)
    {
        // Pass state and control to integrator
		blasfeo_unpack_dvec(nu[i], nlp_out->ux+i, 0, work->sim_in[i]->u);
		blasfeo_unpack_dvec(nx[i], nlp_out->ux+i, nu[i], work->sim_in[i]->x);
        args->sim_solvers[i]->fun(work->sim_in[i], work->sim_out[i], args->sim_solvers_args[i],
            mem->sim_solvers_mem[i], work->sim_solvers_work[i]);

        // TODO(rien): transition functions for changing dimensions not yet implemented!

        // convert b
        blasfeo_pack_dvec(nx[i+1], work->sim_out[i]->xn, &sb[i], 0);
		blasfeo_daxpy(nx[i+1], -1.0, nlp_out->ux+i+1, nu[i+1], sb+i, 0, sb+i, 0);
        // copy B
        blasfeo_pack_tran_dmat(nx[i+1], nu[i], &work->sim_out[i]->S_forw[nx[i+1]*nx[i]], nx[i+1], &sBAbt[i], 0, 0);
        // copy A
        blasfeo_pack_tran_dmat(nx[i+1], nx[i], &work->sim_out[i]->S_forw[0], nx[i+1], &sBAbt[i], nu[i], 0);
        // copy b
        blasfeo_drowin(nx[i+1], 1.0, &sb[i], 0, &sBAbt[i], nu[i]+nx[i], 0);


		blasfeo_dvecex_sp(nb[i], 1.0, nlp_in->idxb[i], nlp_out->ux+i, 0, tmp_nbg+i, 0);
		blasfeo_daxpy(nb[i], -1.0, tmp_nbg+i, 0, nlp_in->d+i, 0, sd+i, 0);
		blasfeo_daxpy(nb[i], -1.0, nlp_in->d+i, nb[i]+ng[i], tmp_nbg+i, 0, sd+i, nb[i]+ng[i]);

        // Update gradients
        // TODO(rien): only for diagonal Q, R matrices atm
        // TODO(rien): only for least squares cost with state and control reference atm
        sim_rk_opts *opts = (sim_rk_opts*) args->sim_solvers_args[i];

		
		blasfeo_pack_dvec(nu[i], y_ref[i]+nx[i], tmp_nux+i, 0);
		blasfeo_pack_dvec(nx[i], y_ref[i], tmp_nux+i, nu[i]);
		blasfeo_daxpy(nu[i]+nx[i], -1.0, tmp_nux+i, 0, nlp_out->ux+i, 0, tmp_nux+i, 0);

        blasfeo_dsymv_l(nu[i]+nx[i], nu[i]+nx[i], 1.0, &sRSQrq[i], 0, 0, &tmp_nux[i], 0, 0.0, &srq[i], 0, &srq[i], 0);

        if (opts->scheme != NULL && opts->scheme->type != exact)
        {
            for (int_t j = 0; j < nx[i]; j++)
                DVECEL_LIBSTR(&srq[i], nu[i]+j) += work->sim_out[i]->grad[j];
            for (int_t j = 0; j < nu[i]; j++)
                DVECEL_LIBSTR(&srq[i], j) += work->sim_out[i]->grad[nx[i]+j];
        }
        blasfeo_drowin(nu[i]+nx[i], 1.0, &srq[i], 0, &sRSQrq[i], nu[i]+nx[i], 0);

        // for (int_t j = 0; j < nx[i]; j++) {
        //     qp_q[i][j] = cost->W[i][j*(nx[i]+nu[i]+1)]*(w[w_idx+j]-y_ref[i][j]);
        //     // adjoint-based gradient correction:
        //     if (opts->scheme.type != exact) qp_q[i][j] += sim[i].out->grad[j];
        // }
        // for (int_t j = 0; j < nu[i]; j++) {
        //     qp_r[i][j] = cost->W[i][(nx[i]+j)*(nx[i]+nu[i]+1)]*(w[w_idx+nx[i]+j]-y_ref[i][nx[i]+j]);
        //     // adjoint-based gradient correction:
        //     if (opts->scheme.type != exact) qp_r[i][j] += sim[i].out->grad[nx[i]+j];
        // }
    }

	i = N;
	blasfeo_dvecex_sp(nb[i], 1.0, nlp_in->idxb[i], nlp_out->ux+i, 0, tmp_nbg+i, 0);
	blasfeo_daxpy(nb[i], -1.0, tmp_nbg+i, 0, nlp_in->d+i, 0, sd+i, 0);
	blasfeo_daxpy(nb[i], -1.0, nlp_in->d+i, nb[i]+ng[i], tmp_nbg+i, 0, sd+i, nb[i]+ng[i]);

	blasfeo_pack_dvec(nu[i], y_ref[i]+nx[i], tmp_nux+i, 0);
	blasfeo_pack_dvec(nx[i], y_ref[i], tmp_nux+i, nu[i]);
	blasfeo_daxpy(nu[i]+nx[i], -1.0, tmp_nux+i, 0, nlp_out->ux+i, 0, tmp_nux+i, 0);


    blasfeo_dsymv_l(nx[N], nx[N], 1.0, &sRSQrq[N], 0, 0, &tmp_nux[N], 0, 0.0, &srq[N], 0, &srq[N], 0);

    blasfeo_drowin(nx[N], 1.0, &srq[N], 0, &sRSQrq[N], nx[N], 0);

	return;

}



static void update_variables(const ocp_nlp_out *nlp_out, ocp_nlp_gn_sqp_args *args, ocp_nlp_gn_sqp_memory *mem, ocp_nlp_gn_sqp_work *work)
{

	// loop index
	int i, j;

	// extract dims
    int N = nlp_out->dims->N;
    int *nx = nlp_out->dims->nx;
    int *nu = nlp_out->dims->nu;
    int *nb = nlp_out->dims->nb;
    int *ng = nlp_out->dims->ng;


    for (i = 0; i < N; i++)
    {
        for (j = 0; j < nx[i+1]; j++)
        {
            work->sim_in[i]->S_adj[j] = -DVECEL_LIBSTR(&work->qp_out->pi[i], j);
            // sim[i].in->S_adj[j] = -mem->qp_solver->qp_out->pi[i][j];
        }
    }

	// step in primal variables
	for (i=0; i<=N; i++)
		blasfeo_daxpy(nu[i]+nx[i], 1.0, work->qp_out->ux+i, 0, nlp_out->ux+i, 0, nlp_out->ux+i, 0);

	// absolute in dual variables
	// TODO
#if 1
	for (i=0; i<N; i++)
		blasfeo_dveccp(nx[i+1], work->qp_out->pi+i, 0, nlp_out->pi+i, 0);

	for (i=0; i<=N; i++)
		blasfeo_dveccp(2*nb[i]+2*ng[i], work->qp_out->lam+i, 0, nlp_out->lam+i, 0);
#endif

	return;

}



//static void store_trajectories(const ocp_nlp_in *nlp, ocp_nlp_gn_sqp_memory *memory, ocp_nlp_out *out, real_t *w)
static void store_trajectories(const ocp_nlp_out *nlp_out, ocp_nlp_gn_sqp_memory *memory)
{

	// loop index
	int i, j;

	// extract dims
    int N = nlp_out->dims->N;
    int *nx = nlp_out->dims->nx;
    int *nu = nlp_out->dims->nu;

	blasfeo_unpack_dvec(nu[i], nlp_out->ux+i, 0, memory->u[i]);
	blasfeo_unpack_dvec(nx[i], nlp_out->ux+i, nu[i], memory->u[i]);

	return;

}



// Simple fixed-step Gauss-Newton based SQP routine
int ocp_nlp_gn_sqp(ocp_nlp_in *nlp_in, ocp_nlp_out *nlp_out, ocp_nlp_gn_sqp_args *args, ocp_nlp_gn_sqp_memory *mem, void *work_)
{
    ocp_nlp_gn_sqp_work *work = (ocp_nlp_gn_sqp_work*) work_;
    ocp_nlp_gn_sqp_cast_workspace(work, mem, args);

    int N = nlp_in->dims->N;
    int *nx = nlp_in->dims->nx;
    int *nu = nlp_in->dims->nu;
    int *nb = nlp_in->dims->nb;

    sim_rk_opts *sim_opts;

    // set up integrators
    for (int ii = 0; ii < N; ii++)
    {
        sim_opts = args->sim_solvers_args[ii];
        work->sim_in[ii]->step = sim_opts->interval/sim_opts->num_steps;

        work->sim_in[ii]->vde = nlp_in->vde[ii];
        work->sim_in[ii]->jac = nlp_in->jac[ii];
        work->sim_in[ii]->vde_adj = nlp_in->vde_adj[ii];
        work->sim_in[ii]->forward_vde_wrapper = &vde_fun;
        work->sim_in[ii]->jacobian_wrapper = &jac_fun;
        work->sim_in[ii]->adjoint_vde_wrapper = &vde_hess_fun;

        // TODO(dimitris): REVISE IF THIS IS CORRECT FOR VARYING DIMENSIONS!
        for (int jj = 0; jj < nx[ii+1] * (nx[ii] + nu[ii]); jj++)
            work->sim_in[ii]->S_forw[jj] = 0.0;
        for (int jj = 0; jj < nx[ii+1]; jj++)
            work->sim_in[ii]->S_forw[jj * (nx[ii] + 1)] = 1.0;
        for (int jj = 0; jj < nx[ii] + nu[ii]; jj++)
            work->sim_in[ii]->S_adj[jj] = 0.0;
        // for (int jj = 0; jj < nlp_in->dims->num_stages[ii] * nx[ii+1]; jj++)
            // work->sim_in[ii]->grad_K[jj] = 0.0;
    }

    initialize_objective(nlp_in, args, mem, work);

    initialize_trajectories(nlp_out, mem, work);

    // TODO(dimitris): move somewhere else (not needed after new nlp_in)
    int_t **qp_idxb = (int_t **) work->qp_in->idxb;
    for (int_t i = 0; i <= N; i++)
	{
        for (int_t j = 0; j < nb[i]; j++)
		{
			qp_idxb[i][j] = nlp_in->idxb[i][j];
        }
    }

    int_t max_sqp_iterations =  args->maxIter;

    acados_timer timer;
    real_t total_time = 0;
    acados_tic(&timer);
    for (int_t sqp_iter = 0; sqp_iter < max_sqp_iterations; sqp_iter++)
    {
        multiple_shooting(nlp_in, nlp_out, args, mem, work);

//print_ocp_qp_in(work->qp_in);
//exit(1);

        int_t qp_status = args->qp_solver->fun(work->qp_in, work->qp_out,
            args->qp_solver_args, mem->qp_solver_mem, work->qp_work);

//print_ocp_qp_out(work->qp_out);
//exit(1);

        if (qp_status != 0)
        {
            printf("QP solver returned error status %d\n", qp_status);
            return -1;
        }

        update_variables(nlp_out, args, mem, work);

//ocp_nlp_dims_print(nlp_out->dims);
//ocp_nlp_out_print(nlp_out);
//exit(1);

        for (int_t i = 0; i < N; i++)
        {
            sim_rk_opts *opts = (sim_rk_opts*) args->sim_solvers_args[i];
            if (opts->scheme == NULL)
                continue;
            opts->sens_adj = (opts->scheme->type != exact);
            if (nlp_in->freezeSens) {
                // freeze inexact sensitivities after first SQP iteration !!
                opts->scheme->freeze = true;
            }
        }
    }

    total_time += acados_toc(&timer);
//    store_trajectories(nlp_in, mem, nlp_out, work->w);
    store_trajectories(nlp_out, mem);

//	ocp_nlp_out_print(nlp_out);

    return 0;
}
