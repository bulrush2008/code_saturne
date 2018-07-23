/*============================================================================
 * Build an algebraic CDO face-based system for unsteady convection/diffusion
 * reaction of vector-valued equations with source terms
 *============================================================================*/

/*
  This file is part of Code_Saturne, a general-purpose CFD tool.

  Copyright (C) 1998-2018 EDF S.A.

  This program is free software; you can redistribute it and/or modify it under
  the terms of the GNU General Public License as published by the Free Software
  Foundation; either version 2 of the License, or (at your option) any later
  version.

  This program is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
  details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc., 51 Franklin
  Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/

/*----------------------------------------------------------------------------*/

#include "cs_defs.h"

/*----------------------------------------------------------------------------
 * Standard C library headers
 *----------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <float.h>
#include <assert.h>
#include <string.h>

/*----------------------------------------------------------------------------
 *  Local headers
 *----------------------------------------------------------------------------*/

#include <bft_mem.h>

#include "cs_cdo_bc.h"
#include "cs_cdofb_priv.h"
#include "cs_cdofb_scaleq.h"
#include "cs_cdofb_vecteq.h"
#include "cs_equation_bc.h"
#include "cs_equation_common.h"
#include "cs_equation_priv.h"
#include "cs_log.h"
#include "cs_math.h"
#include "cs_navsto_coupling.h"
#include "cs_navsto_param.h"
#include "cs_post.h"
#include "cs_source_term.h"
#include "cs_static_condensation.h"
#include "cs_timer.h"

/*----------------------------------------------------------------------------
 *  Header for the current file
 *----------------------------------------------------------------------------*/

#include "cs_cdofb_navsto.h"

/*----------------------------------------------------------------------------*/

BEGIN_C_DECLS

/*=============================================================================
 * Additional doxygen documentation
 *============================================================================*/

/*!
 * \file cs_cdofb_navsto.c
 *
 * \brief Routines for building and solving Stokes and Navier-Stokes problem
 *        with CDO face-based schemes
 *
 */

/*! \cond DOXYGEN_SHOULD_SKIP_THIS */

/*=============================================================================
 * Local Macro definitions and structure definitions
 *============================================================================*/

#define CS_CDOFB_NAVSTO_DBG      0
#define CS_CDOFB_NAVSTO_MODULO  10

/*! \struct cs_cdofb_navsto_t
 *  \brief Context related to CDO face-based discretization when dealing with
 *         vector-valued unknowns
 */

typedef struct {

  /*!
   * @name Main field variables
   * Fields for every main variable of the equation. Got from cs_navsto_system_t
   */

  /*! \var velocity
   *  Pointer to \ref cs_field_t (owned by \ref cs_navsto_system_t) containing
   *  the cell DoFs of the velocity
   */

  cs_field_t *velocity;

  /*! \var pressure
   *  Pointer to \ref cs_field_t (owned by \ref cs_navsto_system_t) containing
   *  the cell DoFs of the pressure
   */

  cs_field_t *pressure;

  /*!
   * @}
   * @name Arrays storing face unknowns
   * @{
   */

  /*! \var face_velocity
   *  Degrees of freedom for the velocity at faces
   */

  cs_real_t  *face_velocity;

  /*! \var face_pressure
   *  Degrees of freedom for the pressure at faces. Not always allocated.
   *  It depends on the type of algorithm used to couple the Navier-Stokes
   *  system.
   */

  cs_real_t  *face_pressure;

  /*!
   * @}
   * @name Parameters of the algorithm
   * Easy access to useful features and parameters of the algorithm
   * @{
   */

  /*! \var is_zeta_uniform
   *  Bool telling if the auxiliary parameter zeta is uniform. Not always
   *  necessary: zeta is tipically used in Artificial Compressibility algos
   */

  bool is_zeta_uniform;

  /*!
   * @}
   * @name Performance monitoring
   * Monitoring the efficiency of the algorithm used to solve the Navier-Stokes
   * system
   * @{
   */

  /*! \var timer
   *  Cumulated elapsed time for building and solving the Navier--Stokes system
   */
  cs_timer_counter_t  timer;

  /*! @} */

} cs_cdofb_navsto_t;

/*============================================================================
 * Private variables
 *============================================================================*/

/* Pointer to shared structures */
static const cs_cdo_quantities_t    *cs_shared_quant;
static const cs_cdo_connect_t       *cs_shared_connect;
static const cs_time_step_t         *cs_shared_time_step;
static const cs_matrix_structure_t  *cs_shared_scal_ms;
static const cs_matrix_structure_t  *cs_shared_vect_ms;

static cs_cdofb_navsto_t  *cs_cdofb_navsto_context = NULL;

/* For l3superblock60 algorithm */
static const cs_lnum_t blk_size = 32;

/*============================================================================
 * Private function prototypes
 *============================================================================*/

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Allocate a \ref cs_cdofb_navsto_t structure by default
 *
 * \param[in] nsp    pointer to a \ref cs_navsto_param_t structure
 *
 * \return a pointer to a new allocated \ref cs_cdofb_navsto_t strcuture
 */
/*----------------------------------------------------------------------------*/

static cs_cdofb_navsto_t *
_create_navsto_context(const cs_navsto_param_t  *nsp)
{
  cs_cdofb_navsto_t  *nssc = NULL;

  if (nsp->space_scheme != CS_SPACE_SCHEME_CDOFB)
    bft_error(__FILE__, __LINE__, 0, " %s: Invalid space scheme.\n",
              __func__);

  BFT_MALLOC(nssc, 1, cs_cdofb_navsto_t);

  nssc->velocity = cs_field_by_name("velocity");
  nssc->pressure = cs_field_by_name("pressure");

  nssc->face_velocity = NULL;
  nssc->face_pressure = NULL;

  nssc->is_zeta_uniform = true;

  /* Monitoring */
  CS_TIMER_COUNTER_INIT(nssc->timer);

  return nssc;
}

/*! (DOXYGEN_SHOULD_SKIP_THIS) \endcond */

/*============================================================================
 * Public function prototypes
 *============================================================================*/

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Set shared pointers from the main domain members for CDO face-based
 *         schemes
 *
 * \param[in]  quant       additional mesh quantities struct.
 * \param[in]  connect     pointer to a \ref cs_cdo_connect_t struct.
 * \param[in]  time_step   pointer to a time step structure
 * \param[in]  sms         pointer to a \ref cs_matrix_structure_t structure
 *                         (scalar)
 * \param[in]  vms         pointer to a \ref cs_matrix_structure_t structure
 *                         (vector)
 */
/*----------------------------------------------------------------------------*/

void
cs_cdofb_navsto_init_common(const cs_cdo_quantities_t     *quant,
                            const cs_cdo_connect_t        *connect,
                            const cs_time_step_t          *time_step,
                            const cs_matrix_structure_t   *sms,
                            const cs_matrix_structure_t   *vms)
{
  /* Assign static const pointers */
  cs_shared_quant = quant;
  cs_shared_connect = connect;
  cs_shared_time_step = time_step;

  /*
    Matrix structure related to the algebraic system for scalar-valued equation
  */
  cs_shared_scal_ms = sms;

  /*
    Matrix structure related to the algebraic system for vector-valued equation
  */
  cs_shared_vect_ms = vms;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Initialize a \ref cs_cdofb_navsto_t structure storing in the case of
 *         a Uzawa-Augmented Lagrangian approach
 *
 * \param[in] nsp        pointer to a \ref cs_navsto_param_t structure
 * \param[in] nsc_input  pointer to a \ref cs_navsto_coupling_uzawa_t structure
 */
/*----------------------------------------------------------------------------*/

void
cs_cdofb_navsto_init_uzawa_context(const cs_navsto_param_t     *nsp,
                                   const void                  *nsc_input)
{
  /* Sanity checks */
  assert(nsp != NULL && nsc_input != NULL);

  /* Navier-Navsto scheme context (NSSC) */
  cs_cdofb_navsto_t  *nssc = _create_navsto_context(nsp);

  const cs_navsto_coupling_uzawa_t  *nsc =
    (const cs_navsto_coupling_uzawa_t  *)nsc_input;

  /* TODO */
  CS_UNUSED(nsc);

  cs_cdofb_navsto_context = nssc;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Initialize a \ref cs_cdofb_navsto_t structure storing in the case of
 *         an Artificial Compressibility approach
 *
 * \param[in] nsp    pointer to a \ref cs_navsto_param_t structure
 * \param[in] nsc    pointer to a \ref cs_navsto_coupling_uzawa_t structure
 */
/*----------------------------------------------------------------------------*/

void
cs_cdofb_navsto_init_ac_context(const cs_navsto_param_t   *nsp,
                                const void                *nsc_input)
{
  /* Sanity checks */
  assert(nsp != NULL && nsc_input != NULL);

  /* Navier-Navsto scheme context (NSSC) */
  cs_cdofb_navsto_t  *nssc = _create_navsto_context(nsp);

  const cs_navsto_coupling_ac_t  *nsc =
    (const cs_navsto_coupling_ac_t *)nsc_input;

  cs_cdofb_navsto_context = nssc;

  /* Only one vector equation */
  cs_equation_t *mom_eq = nsc->momentum;

  nssc->face_velocity =
    ((cs_cdofb_vecteq_t *)mom_eq->scheme_context)->face_values;

  nssc->is_zeta_uniform = cs_property_is_uniform(nsc->zeta);

}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Initialize a \ref cs_cdofb_navsto_t structure storing in the case of
 *         an Artificial Compressibility - VPP approach
 *
 * \param[in] nsp    pointer to a \ref cs_navsto_param_t structure
 * \param[in] nsc    pointer to a \ref cs_navsto_coupling_uzawa_t structure
 */
/*----------------------------------------------------------------------------*/

void
cs_cdofb_navsto_init_ac_vpp_context(const cs_navsto_param_t   *nsp,
                                    const void                *nsc_input)
{
  /* Sanity checks */
  assert(nsp != NULL && nsc_input != NULL);

  /* Navier-Navsto scheme context (NSSC) */
  cs_cdofb_navsto_t  *nssc = _create_navsto_context(nsp);

  const cs_navsto_coupling_ac_vpp_t  *nsc =
    (const cs_navsto_coupling_ac_vpp_t *)nsc_input;

  cs_cdofb_navsto_context = nssc;

  /* No scalar equation */
  cs_equation_t *mom_eq = nsc->momentum,
                *grd_eq = nsc->graddiv;

  nssc->is_zeta_uniform = cs_property_is_uniform(nsc->zeta);

  /* TODO: face_velocity? */
  BFT_MALLOC(nssc->face_velocity, 3*cs_shared_quant->n_faces, cs_real_t);

  CS_UNUSED(grd_eq);
  CS_UNUSED(mom_eq);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Initialize a \ref cs_cdofb_navsto_t structure storing in the case of
 *         an incremental Projection approach
 *
 * \param[in] nsp        pointer to a \ref cs_navsto_param_t structure
 * \param[in] nsc_input  pointer to a \ref cs_navsto_coupling_uzawa_t structure
 */
/*----------------------------------------------------------------------------*/

void
cs_cdofb_navsto_init_proj_context(const cs_navsto_param_t    *nsp,
                                  const void                 *nsc_input)
{
  /* Sanity checks */
  assert(nsp != NULL && nsc_input != NULL);

  /* Navier-Navsto scheme context (NSSC) */
  cs_cdofb_navsto_t  *nssc = _create_navsto_context(nsp);

  const cs_navsto_coupling_projection_t  *nsc =
    (const cs_navsto_coupling_projection_t  *)nsc_input;

  cs_cdofb_navsto_context = nssc;

  /* No auxiliary vector equation */
  cs_equation_t *pre_eq = nsc->prediction,
                *cor_eq = nsc->correction;

  nssc->face_velocity =
    ((cs_cdofb_vecteq_t *)pre_eq->scheme_context)->face_values;
  nssc->face_pressure =
    ((cs_cdofb_scaleq_t *)cor_eq->scheme_context)->face_values;

}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Destroy a \ref cs_cdofb_navsto_t structure
 *
 * \param[in]      nsp        pointer to a \ref cs_navsto_param_t structure
 */
/*----------------------------------------------------------------------------*/

void
cs_cdofb_navsto_free_context(const cs_navsto_param_t      *nsp)
{
  CS_UNUSED(nsp);

  cs_cdofb_navsto_t  *nssc = cs_cdofb_navsto_context;

  if (nssc == NULL)
    return;

  /* Free temporary buffers */
  if (nssc->face_velocity != NULL) BFT_FREE(nssc->face_velocity);
  if (nssc->face_pressure != NULL) BFT_FREE(nssc->face_pressure);

  BFT_FREE(nssc);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Solve the Navier-Stokes system with a CDO face-based scheme using
 *         a Uzawa-Lagrangian Augmented approach.
 *
 * \param[in]      mesh        pointer to a \ref cs_mesh_t structure
 * \param[in]      dt_cur      current value of the time step
 * \param[in]      nsp         pointer to a \ref cs_navsto_param_t structure
 * \param[in, out] nsc_input   Navier-Stokes coupling context: pointer to a
 *                             structure cast on-the-fly
 */
/*----------------------------------------------------------------------------*/

void
cs_cdofb_navsto_uzawa_compute(const cs_mesh_t              *mesh,
                              double                        dt_cur,
                              const cs_navsto_param_t      *nsp,
                              void                         *nsc_input)
{
  CS_UNUSED(dt_cur);

  cs_cdofb_navsto_t  *nssc = cs_cdofb_navsto_context;
  cs_navsto_coupling_uzawa_t  *nscc = (cs_navsto_coupling_uzawa_t  *)nsc_input;

  cs_timer_t  t0 = cs_timer_time();

  /* TODO */

  cs_timer_t  t1 = cs_timer_time();
  cs_timer_counter_add_diff(&(nssc->timer), &t0, &t1);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Solve the Navier-Stokes system with a CDO face-based scheme using
 *         an Artificial Compressibility approach.
 *
 * \param[in]      mesh        pointer to a \ref cs_mesh_t structure
 * \param[in]      dt_cur      current value of the time step
 * \param[in]      nsp         pointer to a \ref cs_navsto_param_t structure
 * \param[in, out] nsc_input   Navier-Stokes coupling context: pointer to a
 *                             structure cast on-the-fly
 */
/*----------------------------------------------------------------------------*/

void
cs_cdofb_navsto_ac_compute(const cs_mesh_t              *mesh,
                           double                        dt_cur,
                           const cs_navsto_param_t      *nsp,
                           void                         *nsc_input)
{
  CS_UNUSED(nsp);

  cs_cdofb_navsto_t  *nssc = cs_cdofb_navsto_context;
  cs_navsto_coupling_ac_t  *nscc = (cs_navsto_coupling_ac_t *)nsc_input;

  cs_equation_t *mom_eq = nscc->momentum;

  cs_timer_t  t0 = cs_timer_time();

  /* TODO */

  cs_timer_t  t1 = cs_timer_time();
  cs_timer_counter_add_diff(&(nssc->timer), &t0, &t1);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Solve the Navier-Stokes system with a CDO face-based scheme using
 *         an Artificial Compressibility - VPP approach.
 *
 * \param[in]      mesh        pointer to a \ref cs_mesh_t structure
 * \param[in]      dt_cur      current value of the time step
 * \param[in]      nsp         pointer to a \ref cs_navsto_param_t structure
 * \param[in, out] nsc_input   Navier-Stokes coupling context: pointer to a
 *                             structure cast on-the-fly
 */
/*----------------------------------------------------------------------------*/

void
cs_cdofb_navsto_ac_vpp_compute(const cs_mesh_t              *mesh,
                               double                        dt_cur,
                               const cs_navsto_param_t      *nsp,
                               void                         *nsc_input)
{
  CS_UNUSED(nsp);

  cs_cdofb_navsto_t  *nssc = cs_cdofb_navsto_context;
  cs_navsto_coupling_ac_vpp_t  *nscc = (cs_navsto_coupling_ac_vpp_t *)nsc_input;

  cs_equation_t *mom_eq = nscc->momentum;

  cs_timer_t  t0 = cs_timer_time();

  /* TODO */

  cs_timer_t  t1 = cs_timer_time();
  cs_timer_counter_add_diff(&(nssc->timer), &t0, &t1);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Solve the Navier-Stokes system with a CDO face-based scheme using
 *         an incremental correction-projection approach.
 *
 * \param[in]      mesh        pointer to a \ref cs_mesh_t structure
 * \param[in]      dt_cur      current value of the time step
 * \param[in]      nsp         pointer to a \ref cs_navsto_param_t structure
 * \param[in, out] nsc_input   Navier-Stokes coupling context: pointer to a
 *                             structure cast on-the-fly
 */
/*----------------------------------------------------------------------------*/

void
cs_cdofb_navsto_proj_compute(const cs_mesh_t              *mesh,
                             double                        dt_cur,
                             const cs_navsto_param_t      *nsp,
                             void                         *nsc_input)
{
  CS_UNUSED(dt_cur);

  cs_cdofb_navsto_t  *nssc = cs_cdofb_navsto_context;
  cs_navsto_coupling_projection_t  *nscc =
    (cs_navsto_coupling_projection_t  *)nsc_input;

  cs_timer_t  t0 = cs_timer_time();

  /* TODO */

  cs_timer_t  t1 = cs_timer_time();
  cs_timer_counter_add_diff(&(nssc->timer), &t0, &t1);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Retrieve the values of the velocity on the faces
 *
 * \return a pointer to an array of \ref cs_real_t
 */
/*----------------------------------------------------------------------------*/

cs_real_t *
cs_cdofb_navsto_get_face_velocity(void)
{
  if (cs_cdofb_navsto_context == NULL)
    return NULL;
  else
    return cs_cdofb_navsto_context->face_velocity;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Retrieve the values of the pressure on the faces
 *
 * \return a pointer to an array of  \ref cs_real_t. (warning: may be NULL)
 */
/*----------------------------------------------------------------------------*/

cs_real_t *
cs_cdofb_navsto_get_face_pressure(void)
{
  if (cs_cdofb_navsto_context == NULL)
    return NULL;
  else
    return cs_cdofb_navsto_context->face_pressure;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Store solution(s) of the linear system into a field structure
 *         Update extra-field values if required (for hybrid discretization)
 *
 * \param[in]      solu       solution array
 * \param[in]      rhs        rhs associated to this solution array
 * \param[in]      eqp        pointer to a \ref cs_equation_param_t structure
 * \param[in, out] eqb        pointer to a \ref cs_equation_builder_t structure
 * \param[in, out] data       pointer to \ref cs_cdofb_navsto_t structure
 * \param[in, out] field_val  pointer to the current value of the field
 */
/*----------------------------------------------------------------------------*/

void
cs_cdofb_navsto_update_field(const cs_real_t              *solu,
                             const cs_real_t              *rhs,
                             const cs_equation_param_t    *eqp,
                             cs_equation_builder_t        *eqb,
                             void                         *data,
                             cs_real_t                    *field_val)
{
  CS_UNUSED(rhs);

  cs_cdofb_navsto_t  *eqc = (cs_cdofb_navsto_t *)data;
  cs_timer_t  t0 = cs_timer_time();


  cs_timer_t  t1 = cs_timer_time();
  cs_timer_counter_add_diff(&(eqb->tce), &t0, &t1);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Predefined extra-operations related to this equation
 *
 * \param[in]       eqname     name of the equation
 * \param[in]       field      pointer to a field structure
 * \param[in]       eqp        pointer to a \ref cs_equation_param_t structure
 * \param[in, out]  eqb        pointer to a \ref cs_equation_builder_t structure
 * \param[in, out]  data       pointer to \ref cs_cdofb_navsto_t structure
 */
/*----------------------------------------------------------------------------*/

void
cs_cdofb_navsto_extra_op(const char                 *eqname,
                         const cs_field_t           *field,
                         const cs_equation_param_t  *eqp,
                         cs_equation_builder_t      *eqb,
                         void                       *data)
{
  CS_UNUSED(eqname);  /* avoid a compilation warning */
  CS_UNUSED(eqp);

  char *postlabel = NULL;
  cs_timer_t  t0 = cs_timer_time();


  // TODO

  cs_timer_t  t1 = cs_timer_time();
  cs_timer_counter_add_diff(&(eqb->tce), &t0, &t1);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Make \p values of zero-mean and compute averages on each cells
 *
 * \param[in,out]  values    vector of cell DoFs which should be of zero mean
 * \param[in]      dim       dimension
 *
 */
/*----------------------------------------------------------------------------*/

void
cs_cdofb_navsto_ensure_zero_mean_and_avg(cs_real_t values[],
                                         short int dim)
{
  /* We should ensure that the mean of the pressure is zero. Thus we compute
   * it and subtract it from every value. */
  /* NOTES:
   *  - It could be useful to stored this average somewhere
   *  - The procedure is not optimized (we can avoid setting the average if
   *    it's a value), but it is the only way to allow multiple definitions
   *    and definitions that do not cover all the domain. */

  // TODO: MPI parallel case?

 /*
  * The algorithm used is l3superblock60, based on the article:
  * "Reducing Floating Point Error in Dot Product Using the Superblock Family
  * of Algorithms" by Anthony M. Castaldo, R. Clint Whaley, and Anthony
  * T. Chronopoulos, SIAM J. SCI. COMPUT., Vol. 31, No. 2, pp. 1156--1174
  * 2008 Society for Industrial and Applied Mathematics
  */
  const cs_lnum_t  nc = cs_shared_quant->n_cells;
  const cs_lnum_t  n_blks = nc / blk_size;
  const cs_lnum_t  n_sblks = sqrt(n_blks);
  const cs_lnum_t  blks_in_sblks = (n_sblks > 0) ? n_blks/n_sblks : 0;

  switch (dim) {
  case 1:
    {
    cs_real_t intgr = 0.0;
#   pragma omp parallel  if (nc> CS_THR_MIN)
    {
#     pragma omp for reduction(+:intgr) nowait
      for (cs_lnum_t sid = 0; sid < n_sblks; sid++) { // Loop on slices

        cs_real_t slc_int = 0.0;
        for (cs_lnum_t bid = 0; bid < blks_in_sblks; bid++) {

          const cs_lnum_t  start_id = blk_size * (blks_in_sblks*sid + bid);
          const cs_real_t *blk_val = values + start_id;

          cs_real_t blk_int = 0.0;
          for (cs_lnum_t c = 0; c < blk_size; c++)
            blk_int += blk_val[c];

          slc_int += blk_int;

        } // Loop on blks

        intgr += slc_int;

      } // Loop on slices

#     pragma omp single
      {
        /* Remainder */
        const cs_lnum_t start_id = blk_size * n_sblks*blks_in_sblks;
        const cs_real_t *blk_val = values + start_id;

        cs_real_t blk_int = 0.0;
        for (cs_lnum_t c = 0; c < nc - start_id; c++)
          blk_int += blk_val[c];

        intgr += blk_int;

      } /* Single zone */
      /* END l3superblock60 algorithm */

#if 0
      There already is an implicit barrier at the end of single, otherwise we
      should have put #pragma omp barrier
#endif

      const cs_real_t  g_avg = intgr / cs_shared_quant->vol_tot;
      const cs_real_t *cv = cs_shared_quant->cell_vol;

#     pragma omp for nowait
      for (cs_lnum_t c_id = 0; c_id < nc; c_id++)
        values[c_id] = values[c_id] / cv[c_id] - g_avg;

    } /* Parallel zone */

    } /* End of dim 1 */
    break;

  case 3:
    {
    cs_real_t intgrx = 0., intgry = 0., intgrz = 0.;
#   pragma omp parallel  if (nc> CS_THR_MIN)
    {
#     pragma omp for reduction(+:intgrx, intgry, intgrz) nowait
      for (cs_lnum_t sid = 0; sid < n_sblks; sid++) { // Loop on slices

        cs_real_t slc_int[] = {0., 0., 0.};
        for (cs_lnum_t bid = 0; bid < blks_in_sblks; bid++) {

          const cs_lnum_t  start_id = blk_size * (blks_in_sblks*sid + bid);
          const cs_real_t *blk_val = values + dim*start_id;

          cs_real_t blk_int[] = {0., 0., 0.};
          for (cs_lnum_t c = 0; c < blk_size; c++) {
            blk_int[0] += blk_val[dim*c];
            blk_int[1] += blk_val[dim*c + 1];
            blk_int[2] += blk_val[dim*c + 2];
          }

          slc_int[0] += blk_int[0];
          slc_int[1] += blk_int[1];
          slc_int[2] += blk_int[2];

        } // Loop on blks

        intgrx += slc_int[0];
        intgry += slc_int[1];
        intgrz += slc_int[2];

      } // Loop on slices

#     pragma omp single
      {
        /* Remainder */
        const cs_lnum_t start_id = blk_size * n_sblks*blks_in_sblks;
        const cs_real_t *blk_val = values + dim*start_id;

        cs_real_t blk_int[] = {0., 0., 0.};
        for (cs_lnum_t c = 0; c < nc - start_id; c++) {
          blk_int[0] += blk_val[dim*c];
          blk_int[1] += blk_val[dim*c + 1];
          blk_int[2] += blk_val[dim*c + 2];
        }

        intgrx += blk_int[0];
        intgry += blk_int[1];
        intgrz += blk_int[2];

      } /* Single zone */
      /* END l3superblock60 algorithm */

#if 0
      There already is an implicit barrier at the end of single, otherwise we
      should have put #pragma omp barrier
#endif

      const cs_real_t  ogv = 1. / cs_shared_quant->vol_tot;
      const cs_real_t  g_avg[] = {intgrx * ogv, intgry * ogv, intgrz * ogv};
      const cs_real_t *cv = cs_shared_quant->cell_vol;

#     pragma omp for nowait
      for (cs_lnum_t c_id = 0; c_id < nc; c_id++){
        const cs_real_t ov = 1. / cv[c_id];
        cs_real_t *loc_val = values + dim*c_id;
        loc_val[0] = loc_val[0] * ov - g_avg[0];
        loc_val[1] = loc_val[1] * ov - g_avg[1];
        loc_val[2] = loc_val[2] * ov - g_avg[2];
      }

    } /* Parallel zone */

    } /* End of dim 3 */
    break;

  default:
    bft_error(__FILE__, __LINE__, 0, " %s: Dimension not handle yet\n",
        __func__);
    break;

  } // Switch

}


/*----------------------------------------------------------------------------*/

END_C_DECLS
