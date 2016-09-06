!-------------------------------------------------------------------------------

! This file is part of Code_Saturne, a general-purpose CFD tool.
!
! Copyright (C) 1998-2016 EDF S.A.
!
! This program is free software; you can redistribute it and/or modify it under
! the terms of the GNU General Public License as published by the Free Software
! Foundation; either version 2 of the License, or (at your option) any later
! version.
!
! This program is distributed in the hope that it will be useful, but WITHOUT
! ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
! FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
! details.
!
! You should have received a copy of the GNU General Public License along with
! this program; if not, write to the Free Software Foundation, Inc., 51 Franklin
! Street, Fifth Floor6b1e433ea7e612c18f94b936f94db2eba9e887f2, Boston, MA 02110-1301, USA.

!-------------------------------------------------------------------------------

!> \file ecrhis.f90
!> \brief Write plot data
!>
!------------------------------------------------------------------------------

!------------------------------------------------------------------------------
! Arguments
!------------------------------------------------------------------------------
!   mode          name          role
!------------------------------------------------------------------------------
!> \param[in]     modhis        0 or 1: initialize/output; 2: finalize
!______________________________________________________________________________

subroutine ecrhis &
 ( modhis )

!===============================================================================
! Module files
!===============================================================================

use paramx
use numvar
use entsor
use cstnum
use optcal
use parall
use mesh
use post
use field

!===============================================================================

implicit none

! Arguments

integer          modhis

! Local variables

character        nompre*300, nomhis*300
integer          tplnum, ii, lpre, lnom, lng
integer          icap, ipp, pflag
integer          c_id, f_id, f_dim, n_fields
double precision varcap(ncaptm)

integer, dimension(:), allocatable :: lsttmp
double precision, dimension(:,:), allocatable :: xyztmp

double precision, dimension(:,:), pointer :: val_v
double precision, dimension(:), pointer :: val_s

character(len=3), dimension(3), save :: nomext3 = (/'[X]', '[Y]', '[Z]'/)
character(len=4), dimension(6), save :: nomext6 &
  = (/'[XX]', '[YY]', '[ZZ]', '[XY]', '[YZ]', '[XZ]'/)
character(len=4), dimension(9), save :: nomext9 &
  = (/'[XX]', '[XY]', '[XZ]', '[YX]', '[YY]', '[YZ]', '[ZX]', '[ZY]', '[ZZ]'/)

! Time plot number shift (in case multiple routines define plots)

integer  nptpl
data     nptpl /0/
save     nptpl

! Number of passes in this routine

integer  ipass
data     ipass /0/
save     ipass

!===============================================================================
! 0. Local initializations
!===============================================================================

ipass = ipass + 1

!--> If no probe data has been output or there are no probes, return
if ((ipass.eq.1 .and. modhis.eq.2) .or. ncapt.eq.0) return

if (ipass.eq.1) then
  call tplnbr(nptpl)
endif

if (keyvis.lt.0) call field_get_key_id('post_vis', keyvis)

!===============================================================================
! 1. Search for neighboring nodes -> nodcap
!===============================================================================

if (ipass.eq.1) then
  do ii = 1, ncapt
    call findpt                                                        &
    (ncelet, ncel, xyzcen,                                             &
     xyzcap(1,ii), xyzcap(2,ii), xyzcap(3,ii), nodcap(ii), ndrcap(ii))
  enddo
endif

!===============================================================================
! 2. Initialize output
!===============================================================================

! Create directory if required
if (ipass.eq.1 .and. irangp.le.0) then
  call csmkdr(emphis, len(emphis))
endif

if (ipass.eq.1) then

  ! plot prefix
  nompre = trim(adjustl(emphis)) // trim(adjustl(prehis))
  lpre = len_trim(nompre)

  allocate(lsttmp(ncapt))
  allocate(xyztmp(3, ncapt))

  ! Initialize one output per variable

  call field_get_n_fields(n_fields)

  ipp = 0

  do f_id = 0, n_fields - 1

    call field_get_key_int(f_id, keyvis, pflag)
    if (iand(pflag, POST_MONITOR) .eq. 0) cycle

    call field_get_dim (f_id, f_dim)

    do c_id = 1, min(f_dim, 9)

      ipp = ipp + 1

      do ii = 1, ncapt
        lsttmp(ii) = ii
        if (irangp.lt.0 .or. irangp.eq.ndrcap(ii)) then
          xyztmp(1, ii) = xyzcen(1, nodcap(ii))
          xyztmp(2, ii) = xyzcen(2, nodcap(ii))
          xyztmp(3, ii) = xyzcen(3, nodcap(ii))
        endif
        if (irangp.ge.0) then
          lng = 3
          call parbcr(ndrcap(ii), lng , xyztmp(1, ii))
        endif
      enddo

      if (irangp.le.0) then

        call field_get_label(f_id, nomhis)
        nomhis = adjustl(nomhis)
        if (f_dim .eq. 3) then
          nomhis = trim(nomhis) // nomext3(c_id)
        else if (f_dim .eq. 6) then
          nomhis = trim(nomhis) // nomext6(c_id)
        else if (f_dim .eq. 9) then
          nomhis = trim(nomhis) // nomext9(c_id)
        endif
        lnom = len_trim(nomhis)

        tplnum = nptpl + ipp
        call tppini(tplnum, nomhis, nompre, tplfmt, idtvar, &
                    ncapt, lsttmp(1), xyzcap(1,1), lnom, lpre)

      endif ! (irangp.le.0)

    enddo

  enddo

  deallocate(lsttmp)
  deallocate(xyztmp)

endif

!===============================================================================
! 3. Output results
!===============================================================================

if (modhis.eq.0 .or. modhis.eq.1) then

  call field_get_n_fields(n_fields)

  ipp = 0

  ! Loop on fields

  do f_id = 0, n_fields - 1

    call field_get_key_int(f_id, keyvis, pflag)
    if (iand(pflag, POST_MONITOR) .eq. 0) cycle

    call field_get_dim (f_id, f_dim)

    do c_id = 1, min(f_dim, 9)

      ipp = ipp + 1

      ! Case of 1D fields, including moments

      if (f_dim .eq. 1) then

        call field_get_val_s(f_id, val_s)

        do icap = 1, ncapt
          if (irangp.lt.0) then
            varcap(icap) = val_s(nodcap(icap))
          else
            call parhis(nodcap(icap), ndrcap(icap), val_s, varcap(icap))
          endif
        enddo

        if (irangp.le.0) then
          tplnum = nptpl + ipp
          call tplwri(tplnum, tplfmt, ncapt, ntcabs, ttcabs, varcap)
        endif

      else ! For vector field

        call field_get_val_v(f_id, val_v)

        do icap = 1, ncapt
          if (irangp.lt.0 .or. ndrcap(icap).eq.irangp) then
            varcap(icap) = val_v(c_id, nodcap(icap))
          endif
          if (irangp.ge.0) then
            lng = 1
            call parbcr(ndrcap(icap), lng, varcap(icap))
          endif
        enddo

        if (irangp.le.0) then
          tplnum = nptpl + ipp
          call tplwri(tplnum, tplfmt, ncapt, ntcabs, ttcabs, varcap)
        endif

      endif ! Scalar or  vector field

    enddo ! loop on components

  enddo ! loop on fields

endif

!===============================================================================
! 4. Close output
!===============================================================================

if (modhis.eq.2) then

  call field_get_n_fields(n_fields)

  ipp = 0

  do f_id = 0, n_fields - 1

    call field_get_key_int(f_id, keyvis, pflag)
    if (iand(pflag, POST_MONITOR) .eq. 0) cycle

    call field_get_dim (f_id, f_dim)

    do c_id = 1, min(f_dim, 9)

      ipp = ipp + 1

      if (irangp.le.0) then
        tplnum = nptpl + ipp
        call tplend(tplnum, tplfmt)
      endif

    enddo

  enddo

endif

!===============================================================================
! 5. End
!===============================================================================

return
end subroutine
