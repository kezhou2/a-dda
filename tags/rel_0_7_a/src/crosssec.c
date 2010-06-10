/* FILE : crosssec.c
 * AUTH : Martijn Frijlink
 * DESCR: 1) The functions Ext_cross, Abs_cross and Frp_mat are self-explanatory.
 *        2) The function set_parms reads the integration-parameters from file.
 *        3) The function calc_alldir calculates the scattered field in a
 *           predefined set of directions (predefined by the integration-
 *           parameters). The function fill_tab calculates the trigonometric
 *           function-values belonging to these directions.
 *        4) The functions Sca_cross calls the Romberg-routine for integrating
 *           the scattered intensity resulting in the scattering cross section.
 *        5) The functions Asym_parm_x, Asym_parm_y and Asym_parm_z each
 *           calculate the corresponding component of the asymmetry-parameter,
 *           not yet divided by the scattering cross section. It can therefore
 *           also be interpreted as the scattering force in cross section units.
 *
 *        Currently is developed by Maxim Yurkin
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include "cmplx.h"
#include "const.h"
#include "Romberg.h"
#include "crosssec.h"
#include "comm.h"
#include "debug.h"
#include "memory.h"

/* Convert the (Theta,Phi) couple into a linear array index */
#define alldir_index(T,P) (((T)*phi_int.N + (P)))

Parms_1D parms[Narg];

extern doublecomplex *EgridX,*EgridY;
extern double *Egrid_buffer;
extern double *E2_alldir;
extern double *E2_alldir_buffer;
extern doublecomplex *x;     /* solution of the internal field */
extern doublecomplex *p;     /* polarization */
extern doublecomplex *Einc;          /* defined in main */
extern char *material;           /* defined calculator */
extern int Nmat;                /* defined in main */
extern double *DipoleCoord;
extern doublecomplex cc[MAXNMAT][3]; /* defined in main */
extern FILE *logfile;           /* defined in main */
extern char directory[200];     /* defined in main */
extern int store_scat_grid;     /* defined in main */
extern int phi_integr;
extern clock_t            /* defined in main */
  Timing_EField,
  Timing_EField_ad,
  Timing_calc_EField_ad,
  Timing_comm_EField_ad,
  Timing_EField_sg,
  Timing_calc_EField_sg,
  Timing_comm_EField_sg,
  Timing_Integration,
  Timing_FileIO;
extern double WaveNum;
extern char alldir_parms[200];
extern doublecomplex ref_index[MAXNMAT];
extern double kd;
extern double gridspace;
extern double prop[3], incPolX[3], incPolY[3];
extern int ScatRelation;
extern int memory;

double beta_matr[3][3];

extern double alph_deg,bet_deg,gam_deg;
double alph,bet,gam;     /* in radians */

angle_set alpha_int, beta_int, gamma_int;
extern Parms_1D parms_alpha;

angle_set theta_int,phi_int;  /* for alldir */

scat_grid_angles angles;
Parms_1D phi_sg;

/*=====================================================================*/

void init_rotation (void)
   /* initialize matrices used for reference frame transformation */
{
  double ca,sa,cb,sb,cg,sg;
  extern double prop_0[3], incPolY_0[3], incPolX_0[3];
  /* initialization of angle values in radians */
  alph=deg2rad(alph_deg);
  bet=deg2rad(bet_deg);
  gam=deg2rad(gam_deg);
  /* calculation of rotation matrix */
  ca=cos(alph);
  sa=sin(alph);
  cb=cos(bet);
  sb=sin(bet);
  cg=cos(gam);
  sg=sin(gam);

  beta_matr[0][0]=ca*cb*cg-sa*sg;
  beta_matr[0][1]=sa*cb*cg+ca*sg;
  beta_matr[0][2]=-sb*cg;
  beta_matr[1][0]=-ca*cb*sg-sa*cg;
  beta_matr[1][1]=-sa*cb*sg+ca*cg;
  beta_matr[1][2]=sb*sg;
  beta_matr[2][0]=ca*sb;
  beta_matr[2][1]=sa*sb;
  beta_matr[2][2]=cb;
  /* rotation of incident field */
  MatrVec(beta_matr,prop_0,prop);
  MatrVec(beta_matr,incPolY_0,incPolY);
  MatrVec(beta_matr,incPolX_0,incPolX);
}

/*=====================================================================*/

int ReadLine(FILE *file,char *fname,  /* opened file and filename */
              char *buf,int buf_size)  /* buffer for line and its size */
    /* reads the first uncommented line; returns 1 if EOF reached */
{
  while (!feof(file)) {
    fgets(buf,buf_size,file);
    if (*buf!='#') {  /* if uncommented */
      if (strstr(buf,"\n")==NULL)
        LogError(EC_ERROR,ONE,POSIT,"Buffer overflow while reading '%s' (size of uncommented line > %d)",
                    fname,buf_size-1);
      else return 0;   /* complete line is read */
    }
    else while (strstr(buf,"\n")==NULL) fgets(buf,buf_size,file);  /* finish reading the commented line */
  }
  return 1;
}

/*=====================================================================*/

void ReadLineStart(FILE *file,char *fname,  /* opened file and filename */
                   char *buf,int buf_size,  /* buffer for line and its size */
                   char *start)             /* beginning of the line to search */
    /* reads the first uncommented line that starts with 'start' */
{
  while (!feof(file)) {
    fgets(buf,buf_size,file);
    if (strstr(buf,start)==buf) { /* if correct beginning */
      if (strstr(buf,"\n")==NULL)
        LogError(EC_ERROR,ONE,POSIT,"Buffer overflow while reading '%s' (size of essential line > %d)",
                    fname,buf_size-1);
      else return;  /* line found and fits into buffer */
    }
    else while (strstr(buf,"\n")==NULL) fgets(buf,buf_size,file);  /* finish reading unmatched line */
  }
  LogError(EC_ERROR,ONE,POSIT,"String '%s' is not found (in correct place) in file '%s'",start,fname);
}

/*=====================================================================*/

int ScanIntegrParms(FILE *file,char *fname,            /* opened file and filename */
                    angle_set *a,                      /* pointer to angle set */
                    Parms_1D *b,                       /* pointer to parameters of integration */
                    int ifcos,                         /* if space angles equally in cos */
                    char *buf,char* temp,int buf_size) /* 2 buffers and their size */
   /* scan integration parameters for angles from file */
{
  int i;
  double unit;

  /* scan file */
  ReadLineStart(file,fname,buf,buf_size,"min=");
  if (sscanf(buf,"min=%lf",&(a->min))!=1) return 1;
  ReadLineStart(file,fname,buf,buf_size,"max=");
  if (sscanf(buf,"max=%lf",&(a->max))!=1) return 1;
  ReadLineStart(file,fname,buf,buf_size,"JMAX=");
  if (sscanf(buf,"JMAX=%d",&(b->JMAX))!=1) return 1;
  ReadLineStart(file,fname,buf,buf_size,"K=");
  if (sscanf(buf,"K=%d",&(b->K))!=1) return 1;
  ReadLineStart(file,fname,buf,buf_size,"eps=");
  if (sscanf(buf,"eps=%lf",&(b->INT_EPS))!=1) return 1;

  ReadLineStart(file,fname,buf,buf_size,"equiv=");
  if (sscanf(buf,"equiv=%s",temp)!=1) return 1;
  if (strcmp(temp,"true")==0) b->equival=true;
  else if (strcmp(temp,"false")==0) b->equival=false;
  else LogError(EC_ERROR,ONE,POSIT,"Wrong argument of 'equiv' option in file %s",fname);

  /* fill all parameters */
  if (a->min==a->max) {
    a->N=b->Grid_size=1;
    b->JMAX=1;
  }
  else {
    a->N=b->Grid_size=(1 << (b->JMAX-1)) + 1;
    if (b->equival && a->N>1) (a->N)--;
  }
  /* initialize points of integration */
  if ((a->val=(double *) malloc(a->N*sizeof(double)))==NULL)
    LogError(EC_ERROR,ONE,POSIT,"Could not malloc integration array");
  memory += a->N*sizeof(double);

  if (ifcos) {                          /* make equal intervals in cos(angle) */
    b->min=cos(deg2rad(a->max));
    if (fabs(b->min)<1e-15) b->min=0; /* just for convenience */
    b->max=cos(deg2rad(a->min));
    if (b->Grid_size==1) a->val[0]=a->min;
    else {
      unit = (b->max - b->min)/(b->Grid_size-1);
      for (i=0;i<a->N;i++) a->val[i] = rad2deg(acos(b->min+unit*i));
    }
  }
  else {			/* make equal intervals in angle */
    b->min=deg2rad(a->min);
    b->max=deg2rad(a->max);
    if (b->Grid_size==1) a->val[0]=a->min;
    else {
      unit = (a->max - a->min)/(b->Grid_size-1);
      for (i=0;i<a->N;i++) a->val[i] = a->min + unit*i;
    }
  }

  return 0;
}

/*=====================================================================*/

int ScanAngleSet(FILE *file,char *fname,            /* opened file and filename */
                 angle_set *a,                      /* pointers to angle set */
                 char *buf,char *temp,int buf_size) /* 2 buffers and their size */
   /* scan range or set of angles (theta or phi) from file (used for scat_grid) */
{
  int i;
  double min,max,unit;

  ReadLineStart(file,fname,buf,buf_size,"type=");
  if (sscanf(buf,"type=%s",temp)!=1) return -1;
  ReadLineStart(file,fname,buf,buf_size,"N=");
  if (sscanf(buf,"N=%d",&(a->N))!=1) return -1;
  /* initialize angle array */
  if ((a->val=(double *) malloc(a->N*sizeof(double)))==NULL)
    LogError(EC_ERROR,ONE,POSIT,"Could not malloc angle array");
  memory += a->N*sizeof(double);

  if (strcmp(temp,"range")==0) {
    ReadLineStart(file,fname,buf,buf_size,"min=");
    if (sscanf(buf,"min=%lf",&(a->min))!=1) return -1;
    ReadLineStart(file,fname,buf,buf_size,"max=");
    if (sscanf(buf,"max=%lf",&(a->max))!=1) return -1;
    if (a->N==1) a->val[0]=(a->max + a->min)/2;
    else {
      unit = (a->max - a->min)/(a->N - 1);
      for (i=0;i<a->N;i++) a->val[i] = a->min + unit*i;
    }
    return RANGE;
  }
  else if (strcmp(temp,"values")==0) {
    ReadLineStart(file,fname,buf,buf_size,"values=");
    for (i=0;i<a->N;i++) fscanf(file,"%lf\n",a->val+i);
    return VALUES;
  }
  else LogError(EC_ERROR,ONE,POSIT,"Unknown type '%s' in file '%s'",temp,fname);
  /* not actually reached */
  return -1;
}

/*=====================================================================*/

/* for convenience common error in functions ReadAvgParms, ReadAlldirParms, and Read ScatGridParms */
#define ERROR LogError(EC_ERROR,ONE,POSIT,"Wrong format of file '%s'",fname);

void ReadAvgParms(char *fname)
  /* read parameters of orientation averaging from a file */
{
  FILE *input;
  char *buf,*temp;
  unsigned int buf_size=50;
  extern char avg_string[];

  /* allocate buffers */
  if ((buf=(char *)malloc(buf_size*sizeof(char))) == NULL)
    LogError(EC_ERROR,ALL,POSIT,"Could not malloc buf");
  if ((temp=(char *)malloc(buf_size*sizeof(char))) == NULL)
    LogError(EC_ERROR,ALL,POSIT,"Could not malloc temp");
  /* open file */
  if ((input=fopen(fname,"r"))==NULL)
    LogError(EC_ERROR,ONE,POSIT,"Failed to open file '%s'",fname);
  /*scan file */
  ReadLineStart(input,fname,buf,buf_size,"alpha:");
  if (ScanIntegrParms(input,fname,&alpha_int,&parms_alpha,false,buf,temp,buf_size)) ERROR;
  ReadLineStart(input,fname,buf,buf_size,"beta:");
  if (ScanIntegrParms(input,fname,&beta_int,&parms[THETA],true,buf,temp,buf_size)) ERROR;
  ReadLineStart(input,fname,buf,buf_size,"gamma:");
  if (ScanIntegrParms(input,fname,&gamma_int,&parms[PHI],false,buf,temp,buf_size)) ERROR;
  /* free buffers; close file */
  free(buf);
  free(temp);
  fclose(input);
  /* print info to string */
  sprintz(avg_string,
    "alpha: from %g to %g in %d steps\n"\
    "beta: from %g to %g in (up to) %d steps (equally spaced in cosine values)\n"\
    "gamma: from %g to %g in (up to) %d steps\n"\
    "see file 'log_orient_avg' for details\n",
    alpha_int.min,alpha_int.max,alpha_int.N,beta_int.min,beta_int.max,beta_int.N,
    gamma_int.min,gamma_int.max,gamma_int.N);

  D("ReadAvgParms complete");
}
/*=====================================================================*/

void ReadAlldirParms(char *fname)
   /* read integration parameters for asymmetry-paramter & C_sca
      should not be used together with orientation averaging because
      they use the same storage space - parms */
{
  FILE *input;
  char *buf,*temp;
  unsigned int buf_size=50;

  /* allocate buffers */
  if ((buf=(char *)malloc(buf_size*sizeof(char))) == NULL)
    LogError(EC_ERROR,ALL,POSIT,"Could not malloc buf");
  if ((temp=(char *)malloc(buf_size*sizeof(char))) == NULL)
    LogError(EC_ERROR,ALL,POSIT,"Could not malloc temp");
  /* open file */
  if ((input=fopen(fname,"r"))==NULL)
    LogError(EC_ERROR,ONE,POSIT,"Failed to open file '%s'",fname);
  /*scan file */
  ReadLineStart(input,fname,buf,buf_size,"theta:");
  if (ScanIntegrParms(input,fname,&theta_int,&parms[THETA],true,buf,temp,buf_size)) ERROR;
  ReadLineStart(input,fname,buf,buf_size,"phi:");
  if (ScanIntegrParms(input,fname,&phi_int,&parms[PHI],false,buf,temp,buf_size)) ERROR;
  /* free buffers; close file */
  free(buf);
  free(temp);
  fclose(input);
  /* print info */
  fprintz(logfile,
    "\nScattered field is calculated for all directions (for integrated scattering quantities)\n"\
    "theta: from %g to %g in (up to) %d steps (equally spaced in cosine values)\n"\
    "phi: from %g to %g in (up to) %d steps\n"\
    "see files 'log_int_***' for details\n\n",
    theta_int.min,theta_int.max,theta_int.N,phi_int.min,phi_int.max,phi_int.N);

  D("ReadAlldirParms complete");
}

/*=====================================================================*/

void ReadScatGridParms(char *fname)
   /* read parameters of the grid on which to calculate scattered field */
{
  FILE *input;
  char *buf,*temp;
  unsigned int buf_size=50;
  int theta_type,phi_type,i;

  /* initialize buffers */
  if ((buf=(char *)malloc(buf_size*sizeof(char))) == NULL)
    LogError(EC_ERROR,ALL,POSIT,"Could not malloc buf");
  if ((temp=(char *)malloc(buf_size*sizeof(char))) == NULL)
    LogError(EC_ERROR,ALL,POSIT,"Could not malloc temp");
  /* open file */
  if ((input=fopen(fname,"r"))==NULL)
    LogError(EC_ERROR,ONE,POSIT,"Failed to open file '%s'",fname);
  /* scan file */
  ReadLineStart(input,fname,buf,buf_size,"global_type=");
  if (sscanf(buf,"global_type=%s",temp)!=1) ERROR;
  if (strcmp(temp,"grid")==0) {
    angles.type = GRID;
    ReadLineStart(input,fname,buf,buf_size,"theta:");
    if ((theta_type=ScanAngleSet(input,fname,&(angles.theta),buf,temp,buf_size))<0) ERROR;
    if (phi_integr) {
      ReadLineStart(input,fname,buf,buf_size,"phi_integr:");
      if (ScanIntegrParms(input,fname,&(angles.phi),&phi_sg,false,buf,temp,buf_size)) ERROR;
      phi_type = RANGE;
    }
    else {
      ReadLineStart(input,fname,buf,buf_size,"phi:");
      if ((phi_type=ScanAngleSet(input,fname,&(angles.phi),buf,temp,buf_size))<0) ERROR;
    }
    angles.N=angles.theta.N*angles.phi.N;
  }
  else if (strcmp(temp,"pairs")==0) {
    if (phi_integr)
      LogError(EC_ERROR,ONE,POSIT,"Integration over phi can't be done with 'global_type=pairs'");
    angles.type = PAIRS;
    ReadLineStart(input,fname,buf,buf_size,"N=");
    if (sscanf(buf,"N=%d",&(angles.N))!=1) ERROR;
    angles.theta.N=angles.phi.N=angles.N;
    /* malloc angle arrays */
    if ((angles.theta.val=(double *) malloc(angles.N*sizeof(double)))==NULL)
      LogError(EC_ERROR,ONE,POSIT,"Could not malloc angles.theta.val");
    if ((angles.phi.val=(double *) malloc(angles.N*sizeof(double)))==NULL)
      LogError(EC_ERROR,ONE,POSIT,"Could not malloc angles.phi.val");
    memory += 2*angles.N*sizeof(double);

    ReadLineStart(input,fname,buf,buf_size,"pairs=");
    for (i=0;i<angles.N;i++) fscanf(input,"%lf %lf\n",angles.theta.val+i,angles.phi.val+i);
  }
  else LogError(EC_ERROR,ONE,POSIT,"Unknown global_type '%s' in file '%s'",temp,fname);
  /* free buffers; close file */
  free(buf);
  free(temp);
  fclose(input);
  /* print info */
  fprintz(logfile,"\nScattered field is calculated for multiple directions\n",fname);
  if (angles.type==GRID) {
    if (theta_type==RANGE)
      fprintz(logfile,"theta: from %g to %g in %d steps\n",
              angles.theta.min,angles.theta.max,angles.theta.N);
    else if (theta_type==VALUES)
      fprintz(logfile,"theta: %d given values\n",angles.theta.N);
    if (phi_type==RANGE) {
      fprintz(logfile,"phi: from %g to %g in %d steps\n",
              angles.phi.min,angles.phi.max,angles.phi.N);
      if (phi_integr) fprintz(logfile,"(Mueller matrix is integrated over phi)\n");
    }
    else if (phi_type==VALUES)
      fprintz(logfile,"phi: %d given values\n",angles.phi.N);
  }
  else if (angles.type==PAIRS)
    fprintz(logfile,"Total %d given (theta,phi) pairs\n",angles.N);
  fprintz(logfile,"\n");

  D("ReadScatGridParms complete");
#undef ERROR
}

/*=====================================================================*/

void calc_field (doublecomplex *ebuff,  /* where to write calculated scattering amplitude */
	         double *n)             /* scattering direction */
{
  /*  Near-optimal routine to compute the scattered fields at one specific
   *  angle. (more exactly - scattering amplitude)
   */

  double kr,kkk;
  doublecomplex a,m2,dpr;
  doublecomplex sum[3],tbuff[3];
  int i,j,jjj;
  double temp, na;
  double f[3][3];
  doublecomplex mult_mat[MAXNMAT];

  if (ScatRelation==SOrd) {
    /* calculate correction coefficient */
    temp=kd*kd/24;
    for(i=0;i<Nmat;i++) {
      na=DotProd(n,prop);
      cSquare(ref_index[i],m2);
      temp=kd*kd/24;
      mult_mat[i][re]=1-temp*(m2[re]-2*na*ref_index[i][re]+1);
      mult_mat[i][im]=temp*(2*na*ref_index[i][im]-m2[im]);       /* mult_mat=1-(kd^2/24)(m^2-2(n.a)m+1) */
    }
  }
  for(i=0;i<3;i++) sum[i][re]=sum[i][im]=0.0;

  for (j=0;j<local_nvoid_Ndip;++j) {
    jjj=3*j;
    /* kr=k*r.n */
    kr=WaveNum*DotProd(DipoleCoord+3*j,n);
    /* a=exp(-ikr.n) */
    cExp(-kr,a);
                          /* multiply by a correction coefficient */
    if (ScatRelation==SOrd) cMultSelf(a,mult_mat[material[j]]);
    /* sum(P*exp(-ik*r.n)) */
    for(i=0;i<3;i++) {
      sum[i][re]+=p[jjj+i][re]*a[re]-p[jjj+i][im]*a[im];
      sum[i][im]+=p[jjj+i][re]*a[im]+p[jjj+i][im]*a[re];
    }
  } /* end for j */
  /* ebuff=(I-nxn).sum=sum-n*(n.sum) */
  crDotProd(sum,n,dpr);
  cScalMultRVec(n,dpr,tbuff);
  cvSubtr(sum,tbuff,ebuff);

  /* multiply it by (-i*k^3) */
  kkk=WaveNum*WaveNum*WaveNum;
  for(i=0;i<3;i++) {
    temp=ebuff[i][re];
    ebuff[i][re]=ebuff[i][im]*kkk;
    ebuff[i][im]=-temp*kkk;
  }
}

/*=====================================================================*/

double Ext_cross(double *incPol)
   /* Calculate the Extinction cross-section */
{
  doublecomplex ebuff[3],tmp;
  double sum;
  int i;

  calc_field (ebuff,prop);
  crDotProd(ebuff,incPol,tmp);                      /* incPol is real, so no conjugate is needed */
  sum=tmp[re];
  my_inner_product(&sum,double_type,1);
  return 4*PI*sum/(WaveNum*WaveNum);
}

/*=====================================================================*/

double Abs_cross(void)
  /* Calculate the Absorption cross-section for process 0 */
{
  int dip,index,i,j;
  char mat;
  double sum, dummy, tmp, temp1,temp2;
  doublecomplex m2;
  double *m; /* not doublecomplex=double[2] to allow assignment to it */
  double cc_inv_im[MAXNMAT][3];   /* -Im(1/cc)=Im(cc)/|cc|^2 */
  double mult_mat[MAXNMAT];

  if (ScatRelation==DRAINE) {
    /* calculate constant and cc_inv_im */
    dummy = 2*WaveNum*WaveNum*WaveNum/3;
    for (i=0;i<Nmat;i++) for (j=0;j<3;j++) cc_inv_im[i][j]=cc[i][j][im]/cAbs2(cc[i][j]);
    /* main cycle */
    for (dip=0,sum=0;dip<local_nvoid_Ndip;++dip) {
      mat=material[dip];
      index=3*dip;
      /* Im(P.Eexc(*))-(2/3)k^3*|P|^2=|P|^2*(-Im(1/cc)-(2/3)k^3) */
      for(i=0;i<3;i++) sum+=(cc_inv_im[mat][i] - dummy)*cAbs2(p[index+i]);
    }
  }
  else if (ScatRelation==SOrd) {
    /* calculate constants */
    temp1=kd*kd/6;
    temp2=4*PI/(gridspace*gridspace*gridspace);
    for (i=0;i<Nmat;i++) {
      m=ref_index[i];
      cSquare(m,m2);
      m2[re]-=1;
        /* mult_mat=-Im(1/hi)*(1+(kd*Im(m))^2)/d^3;  hi=(m^2-1)/(4*PI)  */
      mult_mat[i]=temp2*m2[im]*(1+temp1*m[im]*m[im])/cAbs2(m2);
    }
    /* main cycle */
    for (dip=0,sum=0;dip<local_nvoid_Ndip;++dip)
      sum+=mult_mat[material[dip]]*cvNorm2(p+3*dip);
  }
  my_inner_product(&sum,double_type,1);
  return 4*PI*WaveNum*sum;
}

/*=====================================================================*/
                 
void CalcAlldir(void)
   /* calculate scattered field in many directions */
{
  int index,npoints,point,i,j;
  clock_t tstart,tstart2;
  double robserver[3],incPolpar[3],incPolper[3],cthet,sthet,cphi,sphi,th,ph;
  doublecomplex ebuff[3];

  /* Calculate field */
  tstart = clock();
  npoints = theta_int.N*phi_int.N;
  printz("Calculating scattered field for the whole solid angle:\n");
  for (i=0,point=0;i<theta_int.N;++i) {
    th=deg2rad(theta_int.val[i]);
    cthet=cos(th);
    sthet=sin(th);
    for (j=0;j<phi_int.N;++j) {
      ph=deg2rad(phi_int.val[j]);
      cphi=cos(ph);
      sphi=sin(ph);
      /* robserver = cos(theta)*prop + sin(theta)*[cos(phi)*incPolX + sin(phi)*incPolY]; */
      LinComb(incPolX,incPolY,cphi,sphi,robserver);
      LinComb(prop,robserver,cthet,sthet,robserver);
      /* calculate scattered field - main bottleneck */
      calc_field(ebuff,robserver);
      /* set Epar and Eper - use E2_alldir array to store them
         this is done to decrease communications in 1.5 times */

      /* incPolper = sin(phi)*incPolX - cos(phi)*incPolY; */
      LinComb(incPolX,incPolY,sphi,-cphi,incPolper);
      /* incPolpar = -sin(theta)*prop + cos(theta)*[cos(phi)*incPolX + sin(phi)*incPolY]; */
      LinComb(incPolX,incPolY,cphi,sphi,incPolpar);
      LinComb(prop,incPolpar,-sthet,cthet,incPolpar);

      index=2*point;
      crDotProd(ebuff,incPolper,((doublecomplex*)E2_alldir)[index]);
      crDotProd(ebuff,incPolpar,((doublecomplex*)E2_alldir)[index+1]);

      point++;
      if (((10*point)%npoints)<10) {
	printz(" %d%%",100*point/npoints);
        fflushz(stdout);
      }
    }
  }
  /* accumulate fields */
  tstart2 = clock();
  accumulate(E2_alldir,4*npoints,E2_alldir_buffer);
  Timing_comm_EField_ad = clock() - tstart2;
  /* calculate square of the field */
  for (point=0;point<npoints;point++)
    E2_alldir[point] = cAbs2(((doublecomplex*)E2_alldir)[2*point]) + cAbs2(((doublecomplex*)E2_alldir)[2*point+1]);
  printz("  done\n");
  fflushz(stdout);
  /* timing */
  Timing_EField_ad = clock() - tstart;
  Timing_calc_EField_ad = Timing_EField_ad - Timing_comm_EField_ad;
  Timing_EField += Timing_EField_ad;
}

/*=====================================================================*/

void CalcScatGrid(char which)
   /* calculate scattered field in many directions */
{
  int index,point,i,j,n;
  clock_t tstart;
  double robserver[3],incPolpar[3],incPolper[3],cthet,sthet,cphi,sphi,th,ph;
  doublecomplex ebuff[3];
  doublecomplex *Egrid; /* either EgridX or EgridY */

  /* Calculate field */
  tstart = clock();
  /* choose which array to fill */
  if (which=='X') Egrid=EgridX;
  else if (which=='Y') Egrid=EgridY;
  /* set type of cycling through angles */
  if (angles.type==GRID) n=angles.phi.N;
  else if (angles.type==PAIRS) n=1;
  printz("Calculating grid of scattered field:\n");
  /* main cycle */
  for (i=0,point=0;i<angles.theta.N;++i) {
    th=deg2rad(angles.theta.val[i]);
    cthet=cos(th);
    sthet=sin(th);
    for (j=0;j<n;++j) {
      if (angles.type==GRID) ph=deg2rad(angles.phi.val[j]);
      else if (angles.type==PAIRS) ph=deg2rad(angles.phi.val[i]);
      cphi=cos(ph);
      sphi=sin(ph);
      /* robserver = cos(theta)*prop + sin(theta)*[cos(phi)*incPolX + sin(phi)*incPolY]; */
      LinComb(incPolX,incPolY,cphi,sphi,robserver);
      LinComb(prop,robserver,cthet,sthet,robserver);
      /* calculate scattered field - main bottleneck */
      calc_field(ebuff,robserver);
      /* set Epar and Eper - use Egrid array to store them
         this is done to decrease communications in 1.5 times */

      /* incPolper = sin(phi)*incPolX - cos(phi)*incPolY; */
      LinComb(incPolX,incPolY,sphi,-cphi,incPolper);
      /* incPolpar = -sin(theta)*prop + cos(theta)*[cos(phi)*incPolX + sin(phi)*incPolY]; */
      LinComb(incPolX,incPolY,cphi,sphi,incPolpar);
      LinComb(prop,incPolpar,-sthet,cthet,incPolpar);

      index=2*point;
      crDotProd(ebuff,incPolper,Egrid[index]);
      crDotProd(ebuff,incPolpar,Egrid[index+1]);

      point++;
      if (((10*point)%angles.N)<10) {
	printz(" %d%%",100*point/angles.N);
        fflushz(stdout);
      }
    }
  }
  /* accumulate fields; timing */
  Timing_calc_EField_sg = clock() - tstart;
  tstart = clock();
  accumulate((double *)Egrid,4*angles.N,Egrid_buffer);
  printz("  done\n");
  fflushz(stdout);
  Timing_comm_EField_sg = clock() - tstart;
  Timing_EField_sg = Timing_calc_EField_sg + Timing_comm_EField_sg;
  Timing_EField += Timing_EField_sg;
}

/*=====================================================================*/

void C_sca_integrand(int theta,int phi, double *res)
{
  res[0]=E2_alldir[alldir_index(theta,phi)];
}

/*=====================================================================*/

double Sca_cross(void)
  /* Calculate the scattering cross section 
   * from the integral */
{
  clock_t tstart;
  char fname[200];
  double res;

  sprintf(fname,"%s/log_int_Csca",directory);

  tstart = clock();
  Romberg2D(parms,C_sca_integrand,1,&res,fname);
  res*=4*PI/(WaveNum*WaveNum);
  Timing_Integration += clock() - tstart;
  return res;
}

/*=====================================================================*/

void g_integrand(int theta,int phi,double *res)
{
  double E_square,th,ph;
  th=deg2rad(theta_int.val[theta]);
  ph=deg2rad(phi_int.val[phi]);

  E_square=E2_alldir[alldir_index(theta,phi)];
  res[0] = E_square*sin(th)*cos(ph);
  res[1] = E_square*sin(th)*sin(ph);
  res[2] = E_square*cos(th);
}
 
/*=====================================================================*/

void Asym_parm(double *vec)
  /* Calculate the unnormalized asymmetry parameter,
   * i.e. not yet normalized by Csca */
{
  int comp;
  clock_t tstart;
  char log_int[200];
 
  sprintf(log_int,"%s/log_int_asym",directory);

  tstart = clock();
  Romberg2D(parms,g_integrand,3,vec,log_int);
  for (comp=0;comp<3;++comp) vec[comp]*=4*PI/(WaveNum*WaveNum);
  Timing_Integration += clock() - tstart;
}

/*=====================================================================*/

void g_x_integrand(int theta,int phi,double *res)
{
  res[0]=E2_alldir[alldir_index(theta,phi)]*sin(deg2rad(theta_int.val[theta]))*cos(deg2rad(phi_int.val[phi]));
}

/*=====================================================================*/

void Asym_parm_x(double *vec)
  /* Calculate the unnormalized asymmetry parameter,
   * i.e. not yet normalized by Csca */
{
  clock_t tstart;
  char log_int[200];

  sprintf(log_int,"%s/log_int_asym_x",directory);
 
  tstart = clock();
  Romberg2D(parms,g_x_integrand,1,vec,log_int);
  vec[0] *= 4*PI/(WaveNum*WaveNum);
  Timing_Integration += clock() - tstart;
}

/*=====================================================================*/

void g_y_integrand(int theta,int phi,double *res)
{
  res[0]=E2_alldir[alldir_index(theta,phi)]*sin(deg2rad(theta_int.val[theta]))*sin(deg2rad(phi_int.val[phi]));
}

/*=====================================================================*/

void Asym_parm_y(double *vec)
  /* Calculate the unnormalized asymmetry parameter,
   * i.e. not yet normalized by Csca */
{
  clock_t tstart;
  char log_int[200];
 
  sprintf(log_int,"%s/log_int_asym_y",directory);
 
  tstart = clock();
  Romberg2D(parms,g_y_integrand,1,vec,log_int);
  vec[0] *= 4*PI/(WaveNum*WaveNum);
  Timing_Integration += clock() - tstart;
}
/*=====================================================================*/

void g_z_integrand(int theta,int phi,double *res)
{
  res[0]=E2_alldir[alldir_index(theta,phi)]*cos(deg2rad(theta_int.val[theta]));
}

/*=====================================================================*/

void Asym_parm_z(double *vec)
  /* Calculate the unnormalized asymmetry parameter,
   * i.e. not yet normalized by Csca */

{
  clock_t tstart;
  char log_int[200];
 
  sprintf(log_int,"%s/log_int_asym_z",directory);
 
  tstart = clock();
  Romberg2D(parms,g_z_integrand,1,vec,log_int);
  vec[0] *= 4*PI/(WaveNum*WaveNum);
  Timing_Integration += clock() - tstart;
}

/*=====================================================================*/

void Frp_mat(double Fsca_tot[3],double *Fsca,
	     double Finc_tot[3],double *Finc,
	     double Frp_tot[3],double *Frp)
   /* Calculate the Radiation Pressure by direct calculation
    * of the scattering force. Per dipole the force of 
    * the incoming photons, the scattering force and the 
    * radiation pressure are calculated as intermediate results */
{
  int j,l,i,comp,index,jjj;
  int local_nvoid_d0, local_nvoid_d1;
  int *nvoid_array;
  char *materialT;
  double *rdipT;
  doublecomplex *pT;
  doublecomplex temp;
  doublecomplex dummy,_E_inc;
  int lll;
  double r,r2;      /* (squared) absolute distance */
  doublecomplex
    n[3],          /* unit vector in the direction of r_{jl}
			* complex will always be zero */
    a,ab1,ab2,     /* see chapter ... */
    c1[3],c2[3],   /* idem */
    x_cg[3],       /* complex conjungate P*_j */
    Pn_j,  /* n_jl.P_l */
    Pn_l,  /* P*_j.n_jl */
    inp;   /* P*_j.P_l */


  for (comp=0;comp<3;++comp) Fsca_tot[comp]=Finc_tot[comp]=Frp_tot[comp]=0.0;
  /* Convert internal fields to dipole moments;
     Calculate incoming force per dipole */
  for (j=0;j<local_nvoid_Ndip;++j) {
    dummy[re]=dummy[im]=0.0;
    for (comp=0;comp<3;++comp) {
      index = 3*j+comp;
      /* Im(P.E*inc) */
      _E_inc[re] = Einc[index][re];
      _E_inc[im] = -Einc[index][im];
      cMult(p[index],_E_inc,temp);
      cAdd(dummy,temp,dummy);
    }
    Finc[3*j+2] = WaveNum*dummy[im]/2;
    Finc_tot[2] += Finc[3*j+2];
  }

  /* Because of the parallelisation by row-block decomposition
     the distributed arrays involved need to be gathered on each node
     a) material -> materialT
     b) DipoleCoord -> rdipT
     c) p -> pT
     */
  /* initialize local_nvoid_d0 and local_nvoid_d1 */
  nvoid_array=ivector(0,nprocs-1);
  nvoid_array[ringid]=local_nvoid_Ndip;
  all_gather(nvoid_array+ringid,nvoid_array,int_type,nprocs);
  local_nvoid_d0=0;
  for (i=0;i<ringid;i++) local_nvoid_d0+=nvoid_array[i];
  local_nvoid_d1=local_nvoid_d0+local_nvoid_Ndip;
  free(nvoid_array);
  /* requires a lot of additional memory */
  if ((materialT = (char *) malloc(nvoid_Ndip*sizeof(char)))==NULL)
    LogError(EC_ERROR,ALL,POSIT,"Could not malloc materialT");
  if ((rdipT = (double *) malloc(3*nvoid_Ndip*sizeof(double)))==NULL)
    LogError(EC_ERROR,ALL,POSIT,"Could not malloc rdipT");
  if ((pT = (doublecomplex *) malloc(3*nvoid_Ndip*sizeof(doublecomplex)))==NULL)
    LogError(EC_ERROR,ALL,POSIT,"Could not malloc pT");

  memcpy(materialT+local_nvoid_d0,material,local_nvoid_Ndip*sizeof(char));
  memcpy(pT+3*local_nvoid_d0,p,3*local_nvoid_Ndip*sizeof(doublecomplex));
  memcpy(rdipT+3*local_nvoid_d0,DipoleCoord,3*local_nvoid_Ndip*sizeof(double));

  all_gather(materialT+local_nvoid_d0,materialT,int_type,local_nvoid_Ndip);
  all_gather(pT+3*local_nvoid_d0,pT,cmplx_type,3*local_nvoid_Ndip);
  all_gather(rdipT+3*local_nvoid_d0,rdipT,double_type,3*local_nvoid_Ndip);

  /* Calculate scattering force per dipole */
  for (j=local_nvoid_d0;j<local_nvoid_d1;++j) {
    int jjj = 3*j;

    for (l=0;l<nvoid_Ndip;++l) if (j!=l) {
      lll = 3*l;
      r2 = 0;
      Pn_j[re]=Pn_j[im]=Pn_l[re]=Pn_l[im]=inp[re]=inp[im]=0.0;

      /* Set distance related variables */
      for (comp=0;comp<3;++comp) {
        n[comp][im] = 0;
        n[comp][re] = rdipT[jjj+comp] - rdipT[lll+comp];
	r2 += n[comp][re]*n[comp][re];
      }
      r = sqrt(r2);
      n[0][re]/=r; n[1][re]/=r; n[2][re]/=r;

      /* Set the scalar products a.b1 and a.b2 */
      a[re] = cos(WaveNum*r);
      a[im] = sin(WaveNum*r);
      ab1[re] = 3/(r2*r2) - WaveNum*WaveNum/r2;
      ab2[re] = -WaveNum*WaveNum/r2;
      ab1[im] = -3*WaveNum/(r*r2);
      ab2[im] = WaveNum*WaveNum*WaveNum/r;
      cMultSelf(ab1,a);
      cMultSelf(ab2,a);

      /* Prepare c1 and c2 */
      for (comp=0;comp<3;++comp) {
	x_cg[comp][re] = pT[jjj+comp][re];
	x_cg[comp][im] = -pT[jjj+comp][im];
	cMult(x_cg[comp],n[comp],temp);
	cAdd(Pn_j,temp,Pn_j);
	cMult(n[comp],pT[lll+comp],temp);
	cAdd(Pn_l,temp,Pn_l);
	cMult(x_cg[comp],pT[lll+comp],temp);
	cAdd(inp,temp,inp);
      }

      for (comp=0;comp<3;++comp) {
	/* Set c1 */
	cMult(Pn_j,Pn_l,temp);
	cMult(n[comp],temp,c1[comp]);
	c1[comp][re] *= -5;
	c1[comp][im] *= -5;

	cMult(inp,n[comp],temp);
	cAdd(c1[comp],temp,c1[comp]);
	cMult(Pn_j,pT[lll+comp],temp);
	cAdd(c1[comp],temp,c1[comp]);
	cMult(x_cg[comp],Pn_l,temp);
	cAdd(c1[comp],temp,c1[comp]);

	/* Set c2 */
	cMult(Pn_j,Pn_l,temp);
	cMult(n[comp],temp,c2[comp]);
        c2[comp][re] *= -1;
	c2[comp][im] *= -1;

	cMult(inp,n[comp],temp);
	cAdd(c2[comp],temp,c2[comp]);

	/* Fsca_{jl} = ... */
	cMultSelf(c1[comp],ab1);
	cMultSelf(c2[comp],ab2);
	Fsca[jjj-3*local_d0+comp] += (c1[comp][re] + c2[comp][re])/2;
      }
    } /* end l-loop */

    /* Concluding */
    for (comp=0;comp<3;++comp) {
      Fsca_tot[comp] += Fsca[jjj-3*local_d0+comp];

      Frp[jjj-3*local_d0+comp] = Finc[jjj-3*local_d0+comp] + Fsca[jjj-3*local_d0+comp];

      Frp_tot[comp] += Frp[jjj-3*local_d0+comp];
    }
  } /* end j-loop */

  /* Accumulate the total forces on all nodes */
  my_inner_product(Finc_tot+2,double_type,1);
  my_inner_product(Fsca_tot,double_type,3);
  my_inner_product(Frp_tot,double_type,3);

  free(materialT);
  free(rdipT);
  free(pT);
}