/*
 * $Id$
 * 
 *       This source code is part of
 * 
 *        G   R   O   M   A   C   S
 * 
 * GROningen MAchine for Chemical Simulations
 * 
 *               VERSION 2.0
 * 
 * Copyright (c) 1991-1999
 * BIOSON Research Institute, Dept. of Biophysical Chemistry
 * University of Groningen, The Netherlands
 * 
 * Please refer to:
 * GROMACS: A message-passing parallel molecular dynamics implementation
 * H.J.C. Berendsen, D. van der Spoel and R. van Drunen
 * Comp. Phys. Comm. 91, 43-56 (1995)
 * 
 * Also check out our WWW page:
 * http://md.chem.rug.nl/~gmx
 * or e-mail to:
 * gromacs@chem.rug.nl
 * 
 * And Hey:
 * GRowing Old MAkes el Chrono Sweat
 */
static char *SRCID_g_rms_c = "$Id$";

#include "smalloc.h"
#include "math.h"
#include "macros.h"
#include "typedefs.h"
#include "xvgr.h"
#include "copyrite.h"
#include "statutil.h"
#include "string2.h"
#include "vec.h"
#include "rdgroup.h"
#include "pbc.h"
#include "fatal.h"
#include "futil.h"
#include "princ.h"
#include "rmpbc.h"
#include "do_fit.h"
#include "matio.h"
#include "tpxio.h"
#include "cmat.h"
#include "viewit.h"

int main (int argc,char *argv[])
{
  static char *desc[] = {
    "g_rms compares two structures by computing the root mean square",
    "deviation (RMSD) or when [TT]-rho[tt] is specified the size-independent",
    "'rho' similarity parameter, reference Maiorov & Crippen, PROTEINS 22,",
    "273 (1995).[PAR]"
    "Each structure from a trajectory ([TT]-f[tt]) is compared to a",
    "reference structure from a run input file by least-squares fitting",
    "the structures on top of each other. The reference structure is taken",
    "from the structure file ([TT]-s[tt]).[PAR]",
    "With option [TT]-mir[tt] also a comparison with the mirror image of",
    "the reference structure is calculated.[PAR]",
    "Option [TT]-prev[tt] produces the comparison with a previous frame.[PAR]",
    "Option [TT]-m[tt] produces a matrix in [TT].xpm[tt] format of",
    "comparison values of each structure in the trajectory with respect to",
    "each other structure. This file can be visualized with for instance",
    "[TT]xv[tt] and can be converted to postscript with [TT]xpm2ps[tt].[PAR]",
    "All the structures are fitted pairwise.[PAR]",
    "With [TT]-f2[tt], the 'other structures' are taken from a second",
    "trajectory.[PAR]",
    "Option [TT]-bin[tt] does a binary dump of the comparison matrix.[PAR]",
    "Option [TT]-bm[tt] produces a matrix of average bond angle deviations",
    "analogously to the [TT]-m[tt] option. Only bonds between atoms in the",
    "comparison group are considered."
  };
  static bool bPBC=TRUE,bFit=TRUE,bFitAll=TRUE,bRho=FALSE;
  static bool bNano=FALSE,bDeltaLog=FALSE;
  static int  prev=0,freq=1,freq2=1,nlevels=80,avl=0;
  static real rmsd_user_max=-1,rmsd_user_min=-1, 
              bond_user_max=-1,bond_user_min=-1,
              delta_maxy=0.0;
  t_pargs pa[] = {
    { "-rho", FALSE, etBOOL, {&bRho},
      "Calculate Rho in stead of RMSD" },
    { "-pbc", FALSE, etBOOL, {&bPBC},
      "PBC check" },
    { "-fit",FALSE, etBOOL, {&bFit},
      "Fit to reference structure" },
    { "-ns", FALSE, etBOOL, {&bNano}  ,
      "ns on axis instead of ps"},
    { "-prev", FALSE, etINT, {&prev},
      "Compare with previous frame" },
    { "-fitall",FALSE,etBOOL,{&bFitAll},
      "HIDDENFit all pairs of structures in matrix" },
    { "-skip", FALSE, etINT, {&freq},
      "Only write every nr-th frame to matrix" },
    { "-skip2", FALSE, etINT, {&freq2},
      "Only write every nr-th frame to matrix" },
    { "-max", FALSE, etREAL, {&rmsd_user_max},
      "Maximum level in comparison matrix" },
    { "-min", FALSE, etREAL, {&rmsd_user_min},
      "Minimum level in comparison matrix" },
    { "-bmax", FALSE, etREAL, {&bond_user_max},
      "Maximum level in bond angle matrix" },
    { "-bmin", FALSE, etREAL, {&bond_user_min},
      "Minimum level in bond angle matrix" },
    { "-nlevels", FALSE, etINT, {&nlevels},
      "Number of levels in the matrices" },
    { "-dlog", FALSE, etBOOL, {&bDeltaLog},
      "HIDDENUse a log x-axis in the delta t matrix"},
    { "-dmax", FALSE, etREAL, {&delta_maxy},
      "HIDDENMaximum level in delta matrix" },
    { "-aver", FALSE, etINT, {&avl},
      "HIDDENAverage over this distance in the RMSD matrix" }
  };
  int        step,nre,natoms,natoms2;
  int        i,j,k,m,teller,teller2,tel_mat,tel_mat2;
#define NFRAME 5000
  int        maxframe=NFRAME,maxframe2=NFRAME;
  real       t,lambda,*w_rls,*w_rms,tmas,*w_rls_m,*w_rms_m;
  bool       bTruncOct,bNorm,bAv,bFreq2,bFile2,bMat,bBond,bDelta,bMirror;
  
  t_topology top;
  t_iatom    *iatom=NULL;

  matrix     box;
  rvec       *x,*xp,*xm=NULL,**mat_x=NULL,**mat_x2,*mat_x2_j=NULL,**mat_b=NULL,
             **mat_b2=NULL,vec;
  int        status;
  char       buf[256],tstr[12];
  
  int        nrms,ncons=0;
  FILE       *fp;
  real       rlstot=0,**rls,**rlsm,*time,*time2,*rlsnorm=NULL,**rmsd_mat=NULL,
             **bond_mat=NULL,
             *axis,*axis2,*del_xaxis,*del_yaxis,
             rmsd_max,rmsd_min,bond_max,bond_min,ang,ipr;
  real       **rmsdav_mat=NULL,av_tot,weight,weight_tot;
  real       **delta=NULL,delta_max,delta_scalex=0,delta_scaley=0,*delta_tot;
  int        delta_xsize=0,del_lev=100,mx,my,abs_my;
  bool       bA1,bA2,bPrev,bTop,*bInMat;
  int        ifit,*irms,ibond=0,*ind_bond=NULL,n_ind_m;
  atom_id    *ind_fit,**ind_rms,*ind_m,*rev_ind_m,*ind_rms_m;
  char       *gn_fit,**gn_rms;
  t_rgb      rlo,rhi;
  t_filenm fnm[] = {
    { efTPS, NULL,  NULL,    ffREAD  },
    { efTRX, "-f",  NULL,    ffREAD  },
    { efTRX, "-f2", NULL,    ffOPTRD },
    { efNDX, NULL,  NULL,    ffOPTRD },
    { efXVG, NULL,  "rmsd",  ffWRITE },
    { efXVG, "-mir","rmsdmir",ffOPTWR},
    { efXVG, "-a",  "avgrp", ffOPTWR },
    { efXVG, "-dist","rmsd-dist", ffOPTWR },
    { efXPM, "-m",  "rmsd",  ffOPTWR },
    { efDAT, "-bin","rmsd",  ffOPTWR },
    { efXPM, "-bm", "bond",  ffOPTWR }
  };
#define NFILE asize(fnm)

  CopyRight(stderr,argv[0]);
  parse_common_args(&argc,argv,PCA_CAN_TIME | PCA_CAN_VIEW,TRUE,
		    NFILE,fnm,asize(pa),pa,asize(desc),desc,0,NULL);
  if (bRho) please_cite(stdout,"Maiorov95");
  
  bMirror= opt2bSet("-mir",NFILE,fnm); /* calc RMSD vs mirror of ref. */
  bFile2 = opt2bSet("-f2",NFILE,fnm);
  bMat   = opt2bSet("-m" ,NFILE,fnm);
  bBond  = opt2bSet("-bm",NFILE,fnm);
  bDelta = (delta_maxy > 0); /* calculate rmsd vs delta t matrix from *
			      *	your RMSD matrix (hidden option       */
  bNorm=opt2bSet("-a",NFILE,fnm);
  bFreq2=opt2parg_bSet("-skip2", asize(pa), pa);
  bPrev = (prev > 0);
  if (bPrev) {
    prev=abs(prev);
    if (freq!=1)
      fprintf(stderr,"WARNING: option -skip also applies to -prev\n");
  }
  
  if (bFile2 && !bMat && !bBond) {
    fprintf(stderr,
	    "WARNING: second trajectory (-f2) useless when not calculating matrix (-m/-bm),\n"
	    "         will not read from %s\n",opt2fn("-f2",NFILE,fnm));
    bFile2=FALSE;
  }
  
  if (bDelta) {
    bMat=TRUE;
    if (bFile2) {
      fprintf(stderr,
	      "WARNING: second trajectory (-f2) useless when making delta matrix,\n"
	      "         will not read from %s\n",
	      opt2fn("-f2",NFILE,fnm));
      bFile2=FALSE;
    }
  }
  
  bTop=read_tps_conf(ftp2fn(efTPS,NFILE,fnm),buf,&top,&xp,NULL,box,TRUE);
  snew(w_rls,top.atoms.nr);
  snew(w_rms,top.atoms.nr);

  if (!bTop && bBond) {
    fprintf(stderr,
	    "WARNING: Need a run input file for bond angle matrix,\n"
	    "         will not calculate bond angle matrix.\n");
    bBond=FALSE;
  }
  
  /*set box type*/
  init_pbc(box,FALSE);

  if (bNano) 
    strcpy(tstr,"Time (ns)");
  else
    strcpy(tstr,"Time (ps)");

  if (bFit)
    fprintf(stderr,"Select group for least squares fit\n");
  else
    fprintf(stderr,"Select group for determining center of mass\n");
  get_index(&(top.atoms),ftp2fn_null(efNDX,NFILE,fnm),
	    1,&ifit,&ind_fit,&gn_fit);
  
  if (ifit < 3) 
    fatal_error(0,"Need >= 3 points to fit!\n");
  
  for(i=0; (i<ifit); i++)
    w_rls[ind_fit[i]]=top.atoms.atom[ind_fit[i]].m;

  if (!bMat && !bBond) {
    fprintf(stderr,"How many groups do you want to compare ? ");
    scanf("%d",&nrms);

    fprintf(stderr,"OK. I will compare %d groups\n",nrms);
  }
  else
    nrms=1;

  snew(gn_rms,nrms);
  snew(ind_rms,nrms);
  snew(irms,nrms);
  
  fprintf(stderr,"Select group%s for comparison\n",(nrms>1) ? "s" : "");
  get_index(&(top.atoms),ftp2fn_null(efNDX,NFILE,fnm),
	    nrms,irms,ind_rms,gn_rms);
  
  if (bNorm) {
    snew(rlsnorm,irms[0]);
  }
  snew(rls,nrms);
  for(j=0; (j<nrms); j++)
    snew(rls[j],maxframe);
  if (bMirror) {
    snew(rlsm,nrms);
    for(j=0; (j<nrms); j++)
      snew(rlsm[j],maxframe);
  }
  snew(time,maxframe);
  snew(time2,maxframe2);
  snew(fp,nrms);
  for(j=0; (j<nrms); j++)
    for(i=0; (i<irms[j]); i++)
      w_rms[ind_rms[j][i]]=top.atoms.atom[ind_rms[j][i]].m;
      
  /* Prepare reference frame */
  if (bPBC)
    rm_pbc(&(top.idef),top.atoms.nr,box,xp,xp);
  reset_x(ifit,ind_fit,top.atoms.nr,NULL,xp,w_rls);
  if (bMirror) {
    /* generate reference structure mirror image: */
    snew(xm, top.atoms.nr);
    for(i=0; i<top.atoms.nr; i++) {
      copy_rvec(xp[i],xm[i]);
      xm[i][XX] = -xm[i][XX];
    }
  }
  
  /* read first frame */
  natoms=read_first_x(&status,opt2fn("-f",NFILE,fnm),&t,&x,box);
  if (bMat || bPrev) {
    snew(mat_x,NFRAME);

    if (bPrev)
      /* With -prev we use all atoms for simplicity */
      n_ind_m = natoms;
    else {
      /* Check which atoms we need (fit/rms) */
      snew(bInMat,natoms);
      for(i=0; i<ifit; i++) {
      bInMat[ind_fit[i]] = TRUE;
      n_ind_m++;
      }
      for(i=0; i<irms[0]; i++)
      if (!bInMat[ind_rms[0][i]]) {
        bInMat[ind_rms[0][i]] = TRUE;
        n_ind_m++;
      }
    }
    /* Make an index of needed atoms */
    snew(ind_m,n_ind_m);
    snew(rev_ind_m,natoms);
    j = 0;
    for(i=0; i<natoms; i++)
      if (bPrev || bInMat[i]) {
      ind_m[j] = i;
      rev_ind_m[i] = j;
      j++;
      }
    snew(w_rls_m,n_ind_m);
    snew(ind_rms_m,irms[0]);
    snew(w_rms_m,n_ind_m);
    for(i=0; i<ifit; i++)
      w_rls_m[rev_ind_m[ind_fit[i]]] = w_rls[ind_fit[i]];
    snew(w_rms_m,n_ind_m);
    for(i=0; i<irms[0]; i++) {
      ind_rms_m[i] = rev_ind_m[ind_rms[0][i]];
      w_rms_m[ind_rms_m[i]] = w_rms[ind_rms[0][i]];
    }
    sfree(rev_ind_m);
    sfree(bInMat);
  }

  if (bBond) {
    iatom=top.idef.il[F_SHAKE].iatoms;
    ncons=top.idef.il[F_SHAKE].nr/3;
    fprintf(stderr,"Found %d bonds in topology\n",ncons);
    snew(ind_bond,ncons);
    ibond=0;
    for (i=0;i<ncons;i++) {
      bA1=FALSE; 
      bA2=FALSE;
      for (j=0; j<irms[0]; j++) {
	if (iatom[3*i+1]==ind_rms[0][j]) bA1=TRUE; 
	if (iatom[3*i+2]==ind_rms[0][j]) bA2=TRUE;
      }
      if (bA1 && bA2) {
	ind_bond[ibond]=i;
	ibond++;
      }
    }
    fprintf(stderr,"Using %d bonds for bond angle matrix\n",ibond);
    if (ibond==0)
      fatal_error(0,"0 bonds found");
    snew(mat_b,NFRAME);
    snew(mat_b2,NFRAME);
  }
  tel_mat = 0;
  teller = 0;
  do {
    if (bPBC) 
      rm_pbc(&(top.idef),natoms,box,x,x);
    
    reset_x(ifit,ind_fit,natoms,NULL,x,w_rls);
    if (bFit)
      /*do the least squares fit to original structure*/
      do_fit(natoms,w_rls,xp,x);

    if (teller % freq == 0) {
      if (bMat || bPrev) {
	if (tel_mat >= NFRAME) 
	  srenew(mat_x,tel_mat+1);
	snew(mat_x[tel_mat],n_ind_m);
	for (i=0;i<n_ind_m;i++)
	  copy_rvec(x[ind_m[i]],mat_x[tel_mat][i]);
      }
      if (bBond) {
	if (tel_mat >= NFRAME) srenew(mat_b,tel_mat+1);
	snew(mat_b[tel_mat],ncons);
	for(i=0;i<ncons;i++) {
	  rvec_sub(x[iatom[3*i+2]],x[iatom[3*i+1]],vec);
	  unitv(vec,vec);
	  copy_rvec(vec,mat_b[tel_mat][i]);
	}
      }
      tel_mat++;
    }

    /*calculate energy of root_least_squares*/
    if (bPrev) {
      j=tel_mat-prev-1;
      if (j<0)
	j=0;
      for (i=0;i<n_ind_m;i++)
	copy_rvec(mat_x[j][i],xp[ind_m[i]]);
      reset_x(ifit,ind_fit,natoms,NULL,xp,w_rls);
      if (bFit)
	do_fit(natoms,w_rls,x,xp);
    }    

    for(j=0; (j<nrms); j++) 
      rls[j][teller] = calc_similar_ind(bRho,irms[j],ind_rms[j],w_rms,x,xp);
    if (bNorm) {
      for(j=0; (j<irms[0]); j++)
	rlsnorm[j] += calc_similar_ind(bRho,1,&(ind_rms[0][j]),w_rms,x,xp);
    } 
    
    if (bMirror) {
      if (bFit)
	/*do the least squares fit to mirror of original structure*/
	do_fit(natoms,w_rls,xm,x);
      
      for(j=0; j<nrms; j++)
	rlsm[j][teller] = calc_similar_ind(bRho,irms[j],ind_rms[j],w_rms,x,xm);
    }
    time[teller]=t;
    if (bNano) time[teller] *= 0.001;

    teller++;
    if (teller >= maxframe) {
      maxframe +=NFRAME;
      srenew(time,maxframe);
      for(j=0; (j<nrms); j++) 
	srenew(rls[j],maxframe);
      if (bMirror)
	for(j=0; (j<nrms); j++) 
	  srenew(rlsm[j],maxframe);
    }
  } while (read_next_x(status,&t,natoms,x,box));
  close_trj(status);

  if (bFile2) {
    fprintf(stderr,"\nWill read second trajectory file\n");
    snew(mat_x2,NFRAME);
    if ((natoms2=read_first_x(&status,opt2fn("-f2",NFILE,fnm),&t,&x,box)) 
	!= natoms) 
      fatal_error(0,"Second trajectory (%d atoms) does not match the first one"
		  " (%d atoms)",natoms2,natoms);
    if (!bFreq2) freq2=freq;
    tel_mat2 = 0;
    teller2 = 0;
    do {
      if (bPBC) 
	rm_pbc(&(top.idef),natoms,box,x,x);

      reset_x(ifit,ind_fit,natoms,NULL,x,w_rls);
      if (bFit)
	/*do the least squares fit to original structure*/
	do_fit(natoms,w_rls,xp,x);

      if (teller2 % freq2 == 0) {
	if (bMat) {
	  if (tel_mat2 >= NFRAME) 
	    srenew(mat_x2,tel_mat2+1);
	  snew(mat_x2[tel_mat2],n_ind_m);
	  for (i=0;i<n_ind_m;i++)
	    copy_rvec(x[ind_m[i]],mat_x2[tel_mat2][i]);
	}
	if (bBond) {
	  if (tel_mat2 >= NFRAME) srenew(mat_b2,tel_mat2+1);
	  snew(mat_b2[tel_mat2],ncons);
	  for(i=0;i<ncons;i++) {
	    rvec_sub(x[iatom[3*i+2]],x[iatom[3*i+1]],vec);
	    unitv(vec,vec);
	    copy_rvec(vec,mat_b2[tel_mat2][i]);
	  }
	}
	tel_mat2++;
      }
      
      time2[teller2]=t;
      if (bNano) time2[teller2] *= 0.001;

      teller2++;
      if (teller2 >= maxframe2) {
	maxframe2 +=NFRAME;
	srenew(time2,maxframe2);
      }
    } while (read_next_x(status,&t,natoms,x,box));
    close_trj(status);
  } else {
    mat_x2=mat_x;
    mat_b2=mat_b;
    time2=time;
    tel_mat2=tel_mat;
    freq2=freq;
  }
  
  if (bMat || bBond) {
    /* calculate RMS matrix */
    fprintf(stderr,"\n");
    if (bMat) {
      fprintf(stderr,"Building comparison matrix, %dx%d elements\n",
	      tel_mat,tel_mat2);
      snew(rmsd_mat,tel_mat);
    }
    if (bBond) {
      fprintf(stderr,"Building bond angle matrix, %dx%d elements\n",
	      tel_mat,tel_mat2);
      snew(bond_mat,tel_mat);
    }
    snew(axis,tel_mat);
    snew(axis2,tel_mat2);
    rmsd_max=0.0;
    rmsd_min=1e10;
    bond_max=0.0;
    bond_min=1e10;
    for(j=0; j<tel_mat2; j++)
      axis2[j]=time2[freq2*j];
    if (bDelta) {
      if (bDeltaLog) {
	delta_scalex=8.0/log(2.0);
	delta_xsize=(int)(log(tel_mat/2)*delta_scalex+0.5)+1;
      }
      else {
	delta_xsize=tel_mat/2;
      }
      delta_scaley=1.0/delta_maxy;
      snew(delta,delta_xsize);
      for(j=0; j<delta_xsize; j++)
	snew(delta[j],del_lev+1);
      if (avl > 0) {
	snew(rmsdav_mat,tel_mat);
	for(j=0; j<tel_mat; j++)
	  snew(rmsdav_mat[j],tel_mat);
      }
    }

    if (bFitAll)
      snew(mat_x2_j,natoms);
    for(i=0; i<tel_mat; i++) {
      axis[i]=time[freq*i];
      fprintf(stderr,"\r element %5d; time %5.2f  ",i,axis[i]);
      if (bMat) snew(rmsd_mat[i],tel_mat2);
      if (bBond) snew(bond_mat[i],tel_mat2); 
      for(j=0; j<tel_mat2; j++) {
	if (bFitAll) {
	  for (k=0;k<n_ind_m;k++)
	    copy_rvec(mat_x2[j][k],mat_x2_j[k]);
	  do_fit(n_ind_m,w_rls_m,mat_x[i],mat_x2_j);
	} else
	  mat_x2_j=mat_x2[j];
	if (bMat) {
	  if (bFile2 || (i<=j)) {
	    rmsd_mat[i][j] =
	      calc_similar_ind(bRho,irms[0],ind_rms_m,
			       w_rms_m,mat_x[i],mat_x2_j);
	    if (rmsd_mat[i][j] > rmsd_max) rmsd_max=rmsd_mat[i][j];
	    if (rmsd_mat[i][j] < rmsd_min) rmsd_min=rmsd_mat[i][j];
	  }
	  else
	    rmsd_mat[i][j]=rmsd_mat[j][i];
	}
	if (bBond) {
	  if (bFile2 || (i<=j)) {
	    ang=0.0;
	    for(m=0;m<ibond;m++) { 
	      ipr = iprod(mat_b[i][ind_bond[m]],mat_b2[j][ind_bond[m]]);
	      if (ipr<1.0)
		ang += acos(ipr);
	    }
	    bond_mat[i][j]=ang*180.0/(M_PI*ibond);
	    if (bond_mat[i][j] > bond_max) bond_max=bond_mat[i][j];
	    if (bond_mat[i][j] < bond_min) bond_min=bond_mat[i][j];
	  } 
	  else
	    bond_mat[i][j]=bond_mat[j][i];
	}
      }
    }
    if (bMat && (avl > 0)) {
      rmsd_max=0.0;
      rmsd_min=0.0;
      for(j=0; j<tel_mat-1; j++) {
	for(i=j+1; i<tel_mat; i++) {
	  av_tot=0;
	  weight_tot=0;
	  for (my=-avl; my<=avl; my++) {
	    if ((j+my>=0) && (j+my<tel_mat)) {
	      abs_my = abs(my);
	      for (mx=-avl; mx<=avl; mx++) {
		if ((i+mx>=0) && (i+mx<tel_mat)) {
		  weight = (real)(avl+1-max(abs(mx),abs_my));
		  av_tot += weight*rmsd_mat[i+mx][j+my];
		  weight_tot+=weight;
		}
	      }
	    }
	  }
	  rmsdav_mat[i][j] = av_tot/weight_tot;
	  rmsdav_mat[j][i] = rmsdav_mat[i][j];
	  if (rmsdav_mat[i][j] > rmsd_max) rmsd_max=rmsdav_mat[i][j];
	}
      }
      rmsd_mat=rmsdav_mat;
    }

    if (bMat) {
      fprintf(stderr,"\nMin. value: %f, Max. value: %f\n",rmsd_min,rmsd_max);  
      rlo.r = 1; rlo.g = 1; rlo.b = 1;
      rhi.r = 0; rhi.g = 0; rhi.b = 0;
      if (rmsd_user_max != -1) rmsd_max=rmsd_user_max;
      if (rmsd_user_min != -1) rmsd_min=rmsd_user_min;
      if ((rmsd_user_max !=  -1) || (rmsd_user_min != -1))
	fprintf(stderr,"Min and Max value set to resp. %f and %f\n",
		rmsd_min,rmsd_max);
      sprintf(buf,"%s %s matrix",gn_rms[0],bRho ? "Rho" : "RMSD");
      write_xpm(opt2FILE("-m",NFILE,fnm,"w"),buf,bRho ? "Rho" : "RMSD (nm)",
		tstr,tstr,tel_mat,tel_mat2,axis,axis2,
		rmsd_mat,rmsd_min,rmsd_max,rlo,rhi,&nlevels);
      /* Print the distribution of RMSD values */
      if (opt2bSet("-dist",NFILE,fnm)) 
	low_rmsd_dist(opt2fn("-dist",NFILE,fnm),rmsd_max,tel_mat,rmsd_mat);
      
      if (bDelta) {
	snew(delta_tot,delta_xsize);
	for(j=0; j<tel_mat-1; j++) {
	  for(i=j+1; i<tel_mat; i++) {
	    mx=i-j ;
	    if (mx < tel_mat/2) {
	      if (bDeltaLog) 
		mx=(int)(log(mx)*delta_scalex+0.5);
	      my=(int)(rmsd_mat[i][j]*delta_scaley*del_lev+0.5);
	      delta_tot[mx] += 1.0;
	      if ((rmsd_mat[i][j]>=0) && (rmsd_mat[i][j]<=delta_maxy))
		delta[mx][my] += 1.0;
	    }
	  }
	}
	delta_max=0;
	for(i=0; i<delta_xsize; i++) {
	  if (delta_tot[i] > 0.0) {
	    delta_tot[i]=1.0/delta_tot[i];
	    for(j=0; j<=del_lev; j++) {
	      delta[i][j] *= delta_tot[i];
	      if (delta[i][j] > delta_max)
		delta_max=delta[i][j];
	    }
	  }
	}
	fprintf(stderr,"Maximum in delta matrix: %f\n",delta_max);
	snew(del_xaxis,delta_xsize);
	snew(del_yaxis,del_lev+1);
	for (i=0; i<delta_xsize; i++)
	  del_xaxis[i]=axis[i]-axis[0];
	for (i=0; i<del_lev+1; i++)
	  del_yaxis[i]=delta_maxy*i/del_lev;
	sprintf(buf,"%s %s vs. delta t",gn_rms[0],bRho ? "Rho" : "RMSD");
	fp = ffopen("delta.xpm","w");
	write_xpm(fp,buf,"density",tstr,bRho ? "Rho" : "RMSD (nm)",
		  delta_xsize,del_lev+1,del_xaxis,del_yaxis,
		  delta,0.0,delta_max,rlo,rhi,&nlevels);
	fclose(fp);
      }
      if (opt2bSet("-bin",NFILE,fnm)) {
	fp=ftp2FILE(efDAT,NFILE,fnm,"wb");
	for(i=0;i<tel_mat;i++) 
	  fwrite(rmsd_mat[i],sizeof(**rmsd_mat),tel_mat,fp);
	fclose(fp);
      }
    }
    if (bBond) {
      fprintf(stderr,"\nMin. angle: %f, Max. angle: %f\n",bond_min,bond_max);
      if (bond_user_max != -1) bond_max=bond_user_max;
      if (bond_user_min != -1) bond_min=bond_user_min;
      if ((bond_user_max !=  -1) || (bond_user_min != -1))
	fprintf(stderr,"Bond angle Min and Max set to:\n"
		"Min. angle: %f, Max. angle: %f\n",bond_min,bond_max);
      rlo.r = 1; rlo.g = 1; rlo.b = 1;
      rhi.r = 0; rhi.g = 0; rhi.b = 0;
      sprintf(buf,"%s av. bond angle deviation",gn_rms[0]);
      write_xpm(opt2FILE("-bm",NFILE,fnm,"w"),buf,"degrees",tstr,tstr,
		tel_mat,tel_mat2,axis,axis2,
		bond_mat,bond_min,bond_max,rlo,rhi,&nlevels);
    }
  }
    
  bAv=opt2bSet("-a",NFILE,fnm);

  /* Write the RMSD's to file */
  if (!bPrev)
    if (bRho)
      sprintf(buf,"Rho Deviation");
    else
      sprintf(buf,"RMS Difference");
  else
    sprintf(buf,"%s with frame %g %s ago",bRho ? "Rho" : "RMSD",
	    time[prev*freq]-time[0], bNano ? "ns" : "ps");
  fp=xvgropen(opt2fn("-o",NFILE,fnm),buf,tstr,bRho ? "Rho" : "RMS (nm)");
  if (nrms == 1)
    fprintf(fp,"@ subtitle \"of %s after lsq fit to %s\"\n",gn_rms[0],gn_fit);
  else {
    fprintf(fp,"@ subtitle \"after lsq fit to %s\"\n",gn_fit);
    xvgr_legend(fp,nrms,gn_rms);
  }
  for(i=0; (i<teller); i++) {
    fprintf(fp,"%8.4f",time[bPrev ? freq*i : i]);
    for(j=0; (j<nrms); j++) {
      fprintf(fp," %8.4f",rls[j][i]);
      if (bAv)
	rlstot+=rls[j][i];
    }
    fprintf(fp,"\n");
  }
  
  if (bMirror) {
    /* Write the mirror RMSD's to file */
    fp=xvgropen(opt2fn("-mir",NFILE,fnm),
		bRho ? "Mirror Rho Difference" : "Mirror RMS Deviation",tstr,
		bRho ? "Mirror Rho" : "Mirror RMS (nm)");
    if (nrms == 1)
      fprintf(fp,"@ subtitle \"of %s after lsq fit to mirror of %s\"\n",
	      gn_rms[0],gn_fit);
    else {
      fprintf(fp,"@ subtitle \"after lsq fit to mirror %s\"\n",gn_fit);
      xvgr_legend(fp,nrms,gn_rms);
    }
    for(i=0; (i<teller); i++) {
      fprintf(fp,"%8.4f",time[i]);
      for(j=0; (j<nrms); j++)
	fprintf(fp," %8.4f",rlsm[j][i]);
      fprintf(fp,"\n");
    }
  }

  if (bAv) {
    fp = xvgropen(opt2fn("-a",NFILE,fnm),
		  bRho ? "Average Rho" : "Average RMS","Residue",
		  bRho ? "Rho" : "RMS (nm)");
    for(j=0; (j<nrms); j++)
      fprintf(fp,"%10d  %10g\n",j,rlstot/teller);
    fclose(fp);
  }

  if (bNorm) {
    fp = xvgropen("aver.xvg",gn_rms[0],"Residue",bRho ? "Rho" : "RMS (nm)");
    for(j=0; (j<irms[0]); j++)
      fprintf(fp,"%10d  %10g\n",j,rlsnorm[j]/teller);
    fclose(fp);
  }
  do_view(opt2fn_null("-a",NFILE,fnm),"-graphtype bar");
  do_view(opt2fn("-o",NFILE,fnm),NULL);
  do_view(opt2fn_null("-mir",NFILE,fnm),NULL);
  do_view(opt2fn_null("-m",NFILE,fnm),NULL);
  do_view(opt2fn_null("-bm",NFILE,fnm),NULL);
  do_view(opt2fn_null("-dist",NFILE,fnm),NULL);
  
  thanx(stderr);
  
  return 0;
}
