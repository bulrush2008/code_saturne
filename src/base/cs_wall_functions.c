/*============================================================================
 * Wall functions
 *============================================================================*/

/*
  This file is part of Code_Saturne, a general-purpose CFD tool.

  Copyright (C) 1998-2014 EDF S.A.

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

#include "cs_config.h"
#include "cs_defs.h"

/*----------------------------------------------------------------------------
 * Standard C library headers
 *----------------------------------------------------------------------------*/

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <float.h>

#if defined(HAVE_MPI)
#include <mpi.h>
#endif

/*----------------------------------------------------------------------------
 * BFT library headers
 *----------------------------------------------------------------------------*/

#include <bft_error.h>
#include <bft_mem.h>
#include <bft_printf.h>

/*----------------------------------------------------------------------------
 *  Local headers
 *----------------------------------------------------------------------------*/

#include "cs_log.h"
#include "cs_mesh.h"
#include "cs_turbulence_model.h"

/*----------------------------------------------------------------------------
 *  Header for the current file
 *----------------------------------------------------------------------------*/

#include "cs_wall_functions.h"

/*----------------------------------------------------------------------------*/

BEGIN_C_DECLS

/*=============================================================================
 * Additional doxygen documentation
 *============================================================================*/

/*!
  \file cs_wall_functions.c
        Wall functions descriptor and computation.
*/
/*----------------------------------------------------------------------------*/

/*! \struct cs_wall_functions_t

  \brief wall functions descriptor.

  Members of this wall functions descriptor are publicly accessible, to allow for concise
  syntax, as it is expected to be used in many places.

  \var  cs_wall_functions_t::iwallf
        wall functions
        - 0: no wall functions
        - 1: one scale of friction velocity (power law)
        - 2: one scale of friction velocity (log law)
        - 3: two scales of friction velocity (log law)
        - 4: two scales of friction velocity (log law)
             scalable wall functions
        - 5: two scales of friction velocities (mixing
             length based on V. Driest analysis)
  \var  cs_wall_functions_t::iwallt
        exchange coefficient correlation
        - 0: not use by default
        - 1: exchange coefficient computed with a correlation
  \var  cs_wall_functions_t::ypluli
        limit value of \f$y^+\f$ for the viscous sublayer

        \ref ypluli depends on the chosen wall function: it is initialised to
        10.88 for the scalable wall function (\ref iwallf=4), otherwise it is
        initialised to \f$1/\kappa\approx 2,38\f$. In LES, \ref ypluli is taken
        by default to be 10.88. Always useful.

*/
/*----------------------------------------------------------------------------*/

/*! \cond DOXYGEN_SHOULD_SKIP_THIS */

/*============================================================================
 * Static global variables
 *============================================================================*/

/* wall functions structure and associated pointer */

static cs_wall_functions_t  _wall_functions =
  {-999, 0, -1e13};

const cs_wall_functions_t  *cs_glob_wall_functions = &_wall_functions;

/*============================================================================
 * Prototypes for functions intended for use only by Fortran wrappers.
 * (descriptions follow, with function bodies).
 *============================================================================*/

void
cs_f_wall_functions_get_pointers(int     **iwallf,
                                 int     **iwallt,
                                 double  **ypluli);

/*============================================================================
 * Fortran wrapper function definitions
 *============================================================================*/

/*----------------------------------------------------------------------------
 * Get pointers to members of the wall functions structure.
 *
 * This function is intended for use by Fortran wrappers, and
 * enables mapping to Fortran global pointers.
 *
 * parameters:
 *   iwallf --> pointer to cs_glob_wall_functions->iwallf
 *   iwallt --> pointer to cs_glob_wall_functions->iwallt
 *   ypluli --> pointer to cs_glob_wall_functions->ypluli
 *----------------------------------------------------------------------------*/

void
cs_f_wall_functions_get_pointers(int     **iwallf,
                                 int     **iwallt,
                                 double  **ypluli)
{
  *iwallf = (int *)&(_wall_functions.iwallf);
  *iwallt = &(_wall_functions.iwallt);
  *ypluli = &(_wall_functions.ypluli);
}

/*! (DOXYGEN_SHOULD_SKIP_THIS) \endcond */

/*============================================================================
 * Public function definitions for Fortran API
 *============================================================================*/

/*----------------------------------------------------------------------------
 * Wrapper to cs_wall_functions_velocity
 *----------------------------------------------------------------------------*/

void CS_PROCF (wallfunctions, WALLFUNCTIONS)
(
 const cs_int_t   *const iwallf,
 const cs_lnum_t  *const ifac,
 const cs_real_t  *const l_visc,
 const cs_real_t  *const t_visc,
 const cs_real_t  *const vel,
 const cs_real_t  *const y,
 const cs_real_t  *const rnnb,
 const cs_real_t  *const kinetic_en,
       cs_int_t         *iuntur,
       cs_lnum_t        *nsubla,
       cs_lnum_t        *nlogla,
       cs_real_t        *ustar,
       cs_real_t        *uk,
       cs_real_t        *yplus,
       cs_real_t        *ypup,
       cs_real_t        *cofimp,
       cs_real_t        *dplus
)
{
  assert(*iwallf >= 0 && *iwallf <= 5);

  cs_wall_functions_velocity((cs_wall_function_type_t)*iwallf,
                             *ifac,
                             *l_visc,
                             *t_visc,
                             *vel,
                             *y,
                             *rnnb,
                             *kinetic_en,
                             iuntur,
                             nsubla,
                             nlogla,
                             ustar,
                             uk,
                             yplus,
                             ypup,
                             cofimp,
                             dplus);
}

/*----------------------------------------------------------------------------
 * Wrapper to cs_wall_functions_scalar
 *----------------------------------------------------------------------------*/

void CS_PROCF (hturbp, HTURBP)
(
 const cs_real_t  *const prl,
 const cs_real_t  *const prt,
 const cs_real_t  *const ckarm,
 const cs_real_t  *const yplus,
 const cs_real_t  *const dplus,
       cs_real_t        *htur,
       cs_real_t        *yplim
)
{
  cs_wall_functions_scalar(*prl,
                           *prt,
                           *ckarm,
                           *yplus,
                           *dplus,
                           htur,
                           yplim);
}

/*=============================================================================
 * Public function definitions
 *============================================================================*/

/*----------------------------------------------------------------------------*/

/*! \brief Compute the friction velocity and \f$y^+\f$ / \f$u^+\f$.

*/
/*------------------------------------------------------------------------------
  Arguments
 ______________________________________________________________________________.
   mode           name          role                                           !
 _____________________________________________________________________________*/
/*!
 * \param[in]     iwallf        wall function type
 * \param[in]     ifac          face number
 * \param[in]     l_visc        kinematic viscosity
 * \param[in]     t_visc        turbulent kinematic viscosity
 * \param[in]     vel           wall projected cell center velocity
 * \param[in]     y             wall distance
 * \param[in]     rnnb          \f$\vec{n}.(\tens{R}\vec{n})\f$
 * \param[in]     kinetic_en    turbulente kinetic energy
 * \param[in]     iuntur        indicator: 0 in the viscous sublayer
 * \param[in]     nsubla        counter of cell in the viscous sublayer
 * \param[in]     nlogla        counter of cell in the log-layer
 * \param[out]    ustar         friction velocity
 * \param[out]    uk            friction velocity
 * \param[out]    yplus         dimensionless distance to the wall
 * \param[out]    ypup          yplus projected vel ratio
 * \param[out]    cofimp        \f$\frac{|U_F|}{|U_I^p|}\f$ to ensure a good
 *                              turbulence production
 * \param[out]    dplus         dimensionless shift to the wall for scalable
 *                              wall functions
 */
/*----------------------------------------------------------------------------*/

void
cs_wall_functions_velocity(cs_wall_function_type_t   iwallf,
                           cs_lnum_t                 ifac,
                           cs_real_t                 l_visc,
                           cs_real_t                 t_visc,
                           cs_real_t                 vel,
                           cs_real_t                 y,
                           cs_real_t                 rnnb,
                           cs_real_t                 kinetic_en,
                           int                      *iuntur,
                           cs_lnum_t                *nsubla,
                           cs_lnum_t                *nlogla,
                           cs_real_t                *ustar,
                           cs_real_t                *uk,
                           cs_real_t                *yplus,
                           cs_real_t                *ypup,
                           cs_real_t                *cofimp,
                           cs_real_t                *dplus)
{
  /* Pseudo shift of the wall, 0 by default */
  *dplus = 0.;

  /* Activation of wall function by default */
  *iuntur = 1;

  switch (iwallf) {
  case CS_WALL_F_DISABLED:
    cs_wall_functions_disabled(l_visc,
                               t_visc,
                               vel,
                               y,
                               iuntur,
                               nsubla,
                               nlogla,
                               ustar,
                               uk,
                               yplus,
                               dplus,
                               ypup,
                               cofimp);
    break;
  case CS_WALL_F_1SCALE_POWER:
    cs_wall_functions_1scale_power(l_visc,
                                   vel,
                                   y,
                                   iuntur,
                                   nsubla,
                                   nlogla,
                                   ustar,
                                   uk,
                                   yplus,
                                   ypup,
                                   cofimp);
    break;
  case CS_WALL_F_1SCALE_LOG:
    cs_wall_functions_1scale_log(ifac,
                                 l_visc,
                                 vel,
                                 y,
                                 iuntur,
                                 nsubla,
                                 nlogla,
                                 ustar,
                                 uk,
                                 yplus,
                                 ypup,
                                 cofimp);
    break;
  case CS_WALL_F_2SCALES_LOG:
    cs_wall_functions_2scales_log(l_visc,
                                  t_visc,
                                  vel,
                                  y,
                                  kinetic_en,
                                  iuntur,
                                  nsubla,
                                  nlogla,
                                  ustar,
                                  uk,
                                  yplus,
                                  ypup,
                                  cofimp);
    break;
  case CS_WALL_F_SCALABLE_2SCALES_LOG:
    cs_wall_functions_2scales_scalable(l_visc,
                                       t_visc,
                                       vel,
                                       y,
                                       kinetic_en,
                                       iuntur,
                                       nsubla,
                                       nlogla,
                                       ustar,
                                       uk,
                                       yplus,
                                       dplus,
                                       ypup,
                                       cofimp);
    break;
  case CS_WALL_F_2SCALES_VDRIEST:
    cs_wall_functions_2scales_vdriest(rnnb,
                                      l_visc,
                                      vel,
                                      y,
                                      kinetic_en,
                                      iuntur,
                                      nsubla,
                                      nlogla,
                                      ustar,
                                      uk,
                                      yplus,
                                      ypup,
                                      cofimp);
    break;
  default:
    break;
  }
}

/*----------------------------------------------------------------------------*/

/*! \brief Compute the correction of the exchange coefficient between the fluid
  and the wall for a turbulent flow.

  This is function of the dimensionless
  distance to the wall \f$ y^+ = \dfrac{\centip \centf u_\star}{\nu}\f$.

  Then the return coefficient reads:
  \f[
  h_{tur} = Pr \dfrac{y^+}{T^+}
  \f]

  This coefficient is computed thanks to a similarity model between
  dynamic viscous sub-layer and themal sub-layer.

  \f$ T^+ \f$ is computed as follows:

  - For a laminar Prandtl number smaller than 0.1 (such as liquid metals),
    the standard model with two sub-layers (Prandtl-Taylor) is used.

  - For a laminar Prandtl number larger than 0.1 (such as liquids and gaz),
    a model with three sub-layers (Arpaci-Larsen) is used.

  The final exchange coefficient is:
  \f[
  h = \dfrac{K}{\centip \centf} h_{tur}
  \f]

*/
/*-----------------------------------------------------------------------------
  Arguments
 ______________________________________________________________________________.
   mode           name          role                                           !
 _____________________________________________________________________________*/
/*!
 * \param[in]     prl           laminar Prandtl number
 * \param[in]     prt           turbulent Prandtl number
 * \param[in]     ckarm         Von Karman constant
 * \param[in]     yplus         dimensionless distance to the wall
 * \param[out]    dplus         dimensionless shift to the wall for scalable
 *                              wall functions
 * \param[out]    htur          corrected exchange coefficient
 * \param[out]    yplim         value of the limit for \f$ y^+ \f$
 */
/*----------------------------------------------------------------------------*/

void
cs_wall_functions_scalar(double  prl,
                         double  prt,
                         double  ckarm,
                         double  yplus,
                         double  dplus,
                         double  *htur,
                         double  *yplim)
{

  /* Local variables */

  double tplus;
  double beta2,a2;
  double yp2;
  double prlm1;

  const double epzero = 1.e-12;

  /*==========================================================================*/

  /*==========================================================================
    1. Initializations
    ==========================================================================*/

  /*==========================================================================*/

  (*htur) = CS_MAX(yplus-dplus,epzero)/CS_MAX(yplus,epzero);

  prlm1 = 0.1;

  /*==========================================================================
    2. Compute htur for small Prandtl numbers
    ==========================================================================*/

  if (prl <= prlm1) {
    (*yplim)   = prt/(prl*ckarm);
    if (yplus > (*yplim)) {
      tplus = prl*(*yplim) + prt/ckarm * log(yplus/(*yplim));
      (*htur) = prl*(yplus-dplus)/tplus;
    }

    /*========================================================================
      3. Compute htur for the model with three sub-layers
      ========================================================================*/

  } else {
    yp2   = ckarm*1000./prt;
    yp2   = sqrt(yp2);
    (*yplim)   = pow(1000./prl,1./3.);

    a2 = 15.*pow(prl,2./3.);
    beta2 = a2 - 500./ pow(yp2,2);

    if (yplus >= (*yplim) && yplus < yp2) {
      tplus = a2 - 500./(yplus*yplus);
      (*htur) = prl*(yplus-dplus)/tplus;
    }

    if (yplus >= yp2) {
      tplus = beta2 + prt/ckarm*log(yplus/yp2);
      (*htur) = prl*(yplus-dplus)/tplus;
    }

  }

}

/*----------------------------------------------------------------------------*/

END_C_DECLS
