/*
 * This file is part of the GROMACS molecular simulation package.
 *
 * Copyright (c) 1991-2000, University of Groningen, The Netherlands.
 * Copyright (c) 2001-2008, The GROMACS development team.
 * Copyright (c) 2013,2014, by the GROMACS development team, led by
 * Mark Abraham, David van der Spoel, Berk Hess, and Erik Lindahl,
 * and including many others, as listed in the AUTHORS file in the
 * top-level source directory and at http://www.gromacs.org.
 *
 * GROMACS is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * GROMACS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GROMACS; if not, see
 * http://www.gnu.org/licenses, or write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 *
 * If you want to redistribute modifications to GROMACS, please
 * consider that scientific software is very special. Version
 * control is crucial - bugs must be traceable. We will be happy to
 * consider code for inclusion in the official distribution, but
 * derived work must not be called official GROMACS. Details are found
 * in the README & COPYING files - if they are missing, get the
 * official version at http://www.gromacs.org.
 *
 * To help us fund GROMACS development, we humbly ask that you cite
 * the research papers on the package. Check out http://www.gromacs.org.
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "typedefs.h"
#include "macros.h"
#include "gromacs/utility/smalloc.h"
#include "gromacs/math/units.h"
#include "gromacs/math/vec.h"
#include "force.h"
#include "gromacs/fileio/confio.h"
#include "names.h"
#include "network.h"
#include "ns.h"
#include "nrnb.h"
#include "bondf.h"
#include "txtdump.h"
#include "qmmm.h"
#include "gromacs/utility/fatalerror.h"

/* ORCA interface routines */

void init_orca(t_QMrec *qm)
{
    char *buf;
    snew(buf, 200);

    /* ORCA settings on the system */
    buf = getenv("BASENAME");
    if (buf)
    {
        snew(qm->orca_basename, 200);
        sscanf(buf, "%s", qm->orca_basename);
    }
    else
    {
        gmx_fatal(FARGS, "$BASENAME not set\n");
    }

    /* ORCA directory on the system */
    snew(buf, 200);
    buf = getenv("ORCA_PATH");

    if (buf)
    {
        snew(qm->orca_dir, 200);
        sscanf(buf, "%s", qm->orca_dir);
    }
    else
    {
        gmx_fatal(FARGS, "$ORCA_PATH not set, check manual\n");
    }

    fprintf(stderr, "Setting ORCA path to: %s...\n", qm->orca_dir);
    fprintf(stderr, "ORCA initialised...\n\n");
    /* since we append the output to the BASENAME.out file,
       we should delete an existent old out-file here. */
    sprintf(buf, "%s.out", qm->orca_basename);
    remove(buf);
}


void write_orca_input(t_forcerec *fr, t_QMrec *qm, t_MMrec *mm)
{
    int        i;
    t_QMMMrec *QMMMrec;
    FILE      *out, *pcFile, *addInputFile, *LJCoeff;
    char      *buf, *orcaInput, *addInputFilename, *LJCoeffFilename, *pcFilename, *exclInName, *exclOutName;

    QMMMrec = fr->qr;

    /* write the first part of the input-file */
    snew(orcaInput, 200);
    sprintf(orcaInput, "%s.inp", qm->orca_basename);
    out = fopen(orcaInput, "w");

    snew(addInputFilename, 200);
    sprintf(addInputFilename, "%s.ORCAINFO", qm->orca_basename);
    addInputFile = fopen(addInputFilename, "r");

    fprintf(out, "#input-file generated by GROMACS\n");

    if (qm->bTS)
    {
        fprintf(out, "!QMMMOpt TightSCF\n");
        fprintf(out, "%s\n", "%geom TS_Search EF end");
    }
    else if (qm->bOPT)
    {
        fprintf(out, "!QMMMOpt TightSCF\n");
    }
    else
    {
        fprintf(out, "!EnGrad TightSCF\n");
    }

    /* here we include the insertion of the additional orca-input */
    snew(buf, 200);
    if (addInputFile != NULL)
    {
        while (!feof(addInputFile))
        {
            if (fgets(buf, 200, addInputFile) != NULL)
            {
                fputs(buf, out);
            }
        }
    }
    else
    {
        gmx_fatal(FARGS, "No information on the calculation given in %s\n", addInputFilename);
    }

    fclose(addInputFile);

    if (qm->bTS || qm->bOPT)
    {
        /* freeze the frontier QM atoms and Link atoms. This is
         * important only if a full QM subsystem optimization is done
         * with a frozen MM environmeent. For dynamics, or gromacs's own
         * optimization routines this is not important.
         */
        /* ORCA reads the exclusions from LJCoeffFilename.Excl,
         * so we have to rename the file
         */
        int didStart = 0;
        for (i = 0; i < qm->nrQMatoms; i++)
        {
            if (qm->frontatoms[i])
            {
                if (!didStart)
                {
                    fprintf(out, "%s\n", "%geom");
                    fprintf(out, "   Constraints \n");
                    didStart = 1;
                }
                fprintf(out, "        {C %d C}\n", i); /* counting from 0 */
            }
        }
        if (didStart)
        {
            fprintf(out, "     end\n   end\n");
        }
        /* make a file with information on the C6 and C12 coefficients */
        if (QMMMrec->QMMMscheme != eQMMMschemeoniom && mm->nrMMatoms)
        {
            snew(exclInName, 200);
            snew(exclOutName, 200);
            sprintf(exclInName, "QMMMexcl.dat");
            sprintf(exclOutName, "%s.LJ.Excl", qm->orca_basename);
            rename(exclInName, exclOutName);
            snew(LJCoeffFilename, 200);
            sprintf(LJCoeffFilename, "%s.LJ", qm->orca_basename);
            fprintf(out, "%s%s%s\n", "%LJCOEFFICIENTS \"", LJCoeffFilename, "\"");
            /* make a file with information on the C6 and C12 coefficients */
            LJCoeff = fopen(LJCoeffFilename, "w");
            fprintf(LJCoeff, "%d\n", qm->nrQMatoms);
            for (i = 0; i < qm->nrQMatoms; i++)
            {
#ifdef GMX_DOUBLE
                fprintf(LJCoeff, "%10.7lf  %10.7lf\n", qm->c6[i], qm->c12[i]);
#else
                fprintf(LJCoeff, "%10.7f  %10.7f\n", qm->c6[i], qm->c12[i]);
#endif
            }
            fprintf(LJCoeff, "%d\n", mm->nrMMatoms);
            for (i = 0; i < mm->nrMMatoms; i++)
            {
#ifdef GMX_DOUBLE
                fprintf(LJCoeff, "%10.7lf  %10.7lf\n", mm->c6[i], mm->c12[i]);
#else
                fprintf(LJCoeff, "%10.7f  %10.7f\n", mm->c6[i], mm->c12[i]);
#endif
            }
            fclose(LJCoeff);
        }
    }

    /* write charge and multiplicity */
    fprintf(out, "*xyz %2d%2d\n", qm->QMcharge, qm->multiplicity);

    /* write the QM coordinates */
    for (i = 0; i < qm->nrQMatoms; i++)
    {
        int atomNr;
        if (qm->atomicnumberQM[i] == 0)
        {
            atomNr = 1;
        }
        else
        {
            atomNr = qm->atomicnumberQM[i];
        }
#ifdef GMX_DOUBLE
        fprintf(out, "%3d %10.7lf  %10.7lf  %10.7lf\n",
                atomNr,
                qm->xQM[i][XX]/0.1,
                qm->xQM[i][YY]/0.1,
                qm->xQM[i][ZZ]/0.1);
#else
        fprintf(out, "%3d %10.7f  %10.7f  %10.7f\n",
                atomNr,
                qm->xQM[i][XX]/0.1,
                qm->xQM[i][YY]/0.1,
                qm->xQM[i][ZZ]/0.1);
#endif
    }
    fprintf(out, "*\n");

    /* write the MM point charge data */
    if (QMMMrec->QMMMscheme != eQMMMschemeoniom && mm->nrMMatoms)
    {
        /* name of the point charge file */
        snew(pcFilename, 200);
        sprintf(pcFilename, "%s.pc", qm->orca_basename);
        fprintf(out, "%s%s%s\n", "%pointcharges \"", pcFilename, "\"");
        pcFile = fopen(pcFilename, "w");
        fprintf(pcFile, "%d\n", mm->nrMMatoms);
        for (i = 0; i < mm->nrMMatoms; i++)
        {
#ifdef GMX_DOUBLE
            fprintf(pcFile, "%8.4lf %10.7lf  %10.7lf  %10.7lf\n",
                    mm->MMcharges[i],
                    mm->xMM[i][XX]/0.1,
                    mm->xMM[i][YY]/0.1,
                    mm->xMM[i][ZZ]/0.1);
#else
            fprintf(pcFile, "%8.4f %10.7f  %10.7f  %10.7f\n",
                    mm->MMcharges[i],
                    mm->xMM[i][XX]/0.1,
                    mm->xMM[i][YY]/0.1,
                    mm->xMM[i][ZZ]/0.1);
#endif
        }
        fprintf(pcFile, "\n");
        fclose(pcFile);
    }
    fprintf(out, "\n");

    fclose(out);
}  /* write_orca_input */

real read_orca_output(rvec QMgrad[], rvec MMgrad[], t_forcerec *fr,
                      t_QMrec *qm, t_MMrec *mm)
{
    int
        i, j, atnum;
    char
        buf[300], tmp[300], orca_xyzFilename[300], orca_pcgradFilename[300], orca_engradFilename[300];
    real
        QMener;
    FILE
       *xyz, *pcgrad, *engrad;
    int k;
    t_QMMMrec
       *QMMMrec;
    QMMMrec = fr->qr;
    /* in case of an optimization, the coordinates are printed in the
     * xyz file, the energy and gradients for the QM part are stored in the engrad file
     * and the gradients for the point charges are stored in the pc file.
     */

    /* we need the new xyz coordinates of the QM atoms only for separate QM-optimization
     */

    if (qm->bTS || qm->bOPT)
    {
        sprintf(orca_xyzFilename, "%s.xyz", qm->orca_basename);
        xyz = fopen(orca_xyzFilename, "r");
        if (fgets(buf, 300, xyz) == NULL)
        {
            gmx_fatal(FARGS, "Unexpected end of ORCA output");
        }
        if (fgets(buf, 300, xyz) == NULL)
        {
            gmx_fatal(FARGS, "Unexpected end of ORCA output");
        }
        for (i = 0; i < qm->nrQMatoms; i++)
        {
            if (fgets(buf, 300, xyz) == NULL)
            {
                gmx_fatal(FARGS, "Unexpected end of ORCA output");
            }
#ifdef GMX_DOUBLE
            sscanf(buf, "%s%lf%lf%lf\n",
                   tmp,
                   &qm->xQM[i][XX],
                   &qm->xQM[i][YY],
                   &qm->xQM[i][ZZ]);
#else
            sscanf(buf, "%d%f%f%f\n",
                   &atnum,
                   &qm->xQM[i][XX],
                   &qm->xQM[i][YY],
                   &qm->xQM[i][ZZ]);
#endif
            for (j = 0; j < DIM; j++)
            {
                qm->xQM[i][j] *= 0.1;
            }
        }
        fclose(xyz);
    }
    sprintf(orca_engradFilename, "%s.engrad", qm->orca_basename);
    engrad = fopen(orca_engradFilename, "r");
    /* we read the energy and the gradient for the qm-atoms from the engrad file
     */
    /* we can skip the first seven lines
     */
    for (j = 0; j < 7; j++)
    {
        if (fgets(buf, 300, engrad) == NULL)
        {
            gmx_fatal(FARGS, "Unexpected end of ORCA output");
        }
    }
    /* now comes the energy
     */
    if (fgets(buf, 300, engrad) == NULL)
    {
        gmx_fatal(FARGS, "Unexpected end of ORCA output");
    }
#ifdef GMX_DOUBLE
    sscanf(buf, "%lf\n", &QMener);
#else
    sscanf(buf, "%f\n", &QMener);
#endif
    /* we can skip the next three lines
     */
    for (j = 0; j < 3; j++)
    {
        if (fgets(buf, 300, engrad) == NULL)
        {
            gmx_fatal(FARGS, "Unexpected end of ORCA output");
        }
    }
    /* next lines contain the gradients of the QM atoms
     * now comes the gradient, one value per line:
     * (atom1 x \n atom1 y \n atom1 z \n atom2 x ...
     */

    for (i = 0; i < 3*qm->nrQMatoms; i++)
    {
        k = i/3;
        if (fgets(buf, 300, engrad) == NULL)
        {
            gmx_fatal(FARGS, "Unexpected end of ORCA output");
        }
#ifdef GMX_DOUBLE
        if (i%3 == 0)
        {
            sscanf(buf, "%lf\n", &QMgrad[k][XX]);
        }
        else if (i%3 == 1)
        {
            sscanf(buf, "%lf\n", &QMgrad[k][YY]);
        }
        else if (i%3 == 2)
        {
            sscanf(buf, "%lf\n", &QMgrad[k][ZZ]);
        }
#else
        if (i%3 == 0)
        {
            sscanf(buf, "%f\n", &QMgrad[k][XX]);
        }
        else if (i%3 == 1)
        {
            sscanf(buf, "%f\n", &QMgrad[k][YY]);
        }
        else if (i%3 == 2)
        {
            sscanf(buf, "%f\n", &QMgrad[k][ZZ]);
        }
#endif
    }
    fclose(engrad);
    /* write the MM point charge data
     */
    if (QMMMrec->QMMMscheme != eQMMMschemeoniom && mm->nrMMatoms)
    {
        sprintf(orca_pcgradFilename, "%s.pcgrad", qm->orca_basename);
        pcgrad = fopen(orca_pcgradFilename, "r");

        /* we read the gradient for the mm-atoms from the pcgrad file
         */
        /* we can skip the first line
         */
        if (fgets(buf, 300, pcgrad) == NULL)
        {
            gmx_fatal(FARGS, "Unexpected end of ORCA output");
        }
        for (i = 0; i < mm->nrMMatoms; i++)
        {
            if (fgets(buf, 300, pcgrad) == NULL)
            {
                gmx_fatal(FARGS, "Unexpected end of ORCA output");
            }
    #ifdef GMX_DOUBLE
            sscanf(buf, "%lf%lf%lf\n",
                   &MMgrad[i][XX],
                   &MMgrad[i][YY],
                   &MMgrad[i][ZZ]);
    #else
            sscanf(buf, "%f%f%f\n",
                   &MMgrad[i][XX],
                   &MMgrad[i][YY],
                   &MMgrad[i][ZZ]);
    #endif
        }
        fclose(pcgrad);
    }
    return(QMener);
}

void do_orca(char *orca_dir, char *basename)
{

    /* make the call to the orca binary through system()
     * The location of the binary is set through the
     * environment.
     */
    char
        buf[100];
    sprintf(buf, "%s/%s %s.inp >> %s.out",
            orca_dir,
            "orca",
            basename,
            basename);
    fprintf(stderr, "Calling '%s'\n", buf);
    if (system(buf) != 0)
    {
        gmx_fatal(FARGS, "Call to '%s' failed\n", buf);
    }
}

real call_orca(t_forcerec *fr,
               t_QMrec *qm, t_MMrec *mm, rvec f[], rvec fshift[])
{
    /* normal orca jobs */
    static int
        step = 0;
    int
        i, j;
    real
        QMener;
    rvec
       *QMgrad, *MMgrad;
    char
       *exe;

    snew(exe, 30);
    sprintf(exe, "%s", "orca");
    snew(QMgrad, qm->nrQMatoms);
    snew(MMgrad, mm->nrMMatoms);

    write_orca_input(fr, qm, mm);
    do_orca(qm->orca_dir, qm->orca_basename);
    QMener = read_orca_output(QMgrad, MMgrad, fr, qm, mm);
    /* put the QMMM forces in the force array and to the fshift
     */
    for (i = 0; i < qm->nrQMatoms; i++)
    {
        for (j = 0; j < DIM; j++)
        {
            f[i][j]      = HARTREE_BOHR2MD*QMgrad[i][j];
            fshift[i][j] = HARTREE_BOHR2MD*QMgrad[i][j];
        }
    }
    for (i = 0; i < mm->nrMMatoms; i++)
    {
        for (j = 0; j < DIM; j++)
        {
            f[i+qm->nrQMatoms][j]      = HARTREE_BOHR2MD*MMgrad[i][j];
            fshift[i+qm->nrQMatoms][j] = HARTREE_BOHR2MD*MMgrad[i][j];
        }
    }
    QMener = QMener*HARTREE2KJ*AVOGADRO;
    step++;
    free(exe);
    return(QMener);
} /* call_orca */

/* end of orca sub routines */
