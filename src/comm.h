/* File: comm.h
 * $Author$
 * $Date::                            $
 * Descr: definitions of communication global variables and routines
 *
 * Copyright (C) 2006-2008 University of Amsterdam
 * Copyright (C) 2009 Institute of Chemical Kinetics and Combustion & University of Amsterdam
 * This file is part of ADDA.
 *
 * ADDA is free software: you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * ADDA is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with ADDA. If not, see
 * <http://www.gnu.org/licenses/>.
 */
#ifndef __comm_h
#define __comm_h

#include "types.h"    // needed for doublecomplex
#include "function.h" // for function attributes
#include "timing.h"   // for TIME_TYPE

typedef enum {char_type,int_type,double_type,cmplx_type} var_type;

void Stop(int) ATT_NORETURN;
void Synchronize(void);
void BlockTranspose(doublecomplex *X);
void BlockTranspose_Dm(doublecomplex *X,size_t lengthY,size_t lengthZ);
void AccumulateMax(double *data,double *max);
void Accumulate(double *data,size_t size,double *buffer,TIME_TYPE *timing);
void MyInnerProduct(void *a,var_type type,size_t n_elem,TIME_TYPE *timing);
void AllGather(void *x_from,void *x_to,var_type type,size_t n_elem);
void InitComm(int *argc_p,char ***argv_p);
void ParSetup(void);
void MyBcast(void *data,var_type type,size_t n_elem,TIME_TYPE *timing);
void BcastOrient(int *i,int *j,int *k);
// used by granule generator
void SetGranulComm(double z0,double z1,double gdZ,int gZ,size_t gXY,size_t buf_size,int *lz0,
                   int *lz1,int sm_gr);
void CollectDomainGranul(unsigned char *dom,size_t gXY,int lz0,int locgZ,TIME_TYPE *timing);
void FreeGranulComm(int sm_gr);
void ExchangeFits(char *data,const size_t n,TIME_TYPE *timing);

#ifdef PARALLEL
// this functions are defined only in parallel mode
void CatNFiles(const char *dir,const char *tmpl,const char *dest);

/* analogs of frequently used functions that should be executed only by the root processor
 * !!! not safe if used in constructions like { if (...) PRINTZ(...); else }
 */
#	define PRINTZ if (ringid==ADDA_ROOT) printf
#	define FPRINTZ if (ringid==ADDA_ROOT) fprintf
#	define SPRINTZ if (ringid==ADDA_ROOT) sprintf
#	define STRCPYZ if (ringid==ADDA_ROOT) strcpy
#	define FCLOSEZ if (ringid==ADDA_ROOT) fclose
#	define FFLUSHZ if (ringid==ADDA_ROOT) fflush
#	define PRINTBOTHZ if (ringid==ADDA_ROOT) PrintBoth
#else
#	define PRINTZ printf
#	define FPRINTZ fprintf
#	define SPRINTZ sprintf
#	define STRCPYZ strcpy
#	define FCLOSEZ fclose
#	define FFLUSHZ fflush
#	define PRINTBOTHZ PrintBoth
#endif

#endif // __comm_h