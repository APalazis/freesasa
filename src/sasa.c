/*
  Copyright Simon Mitternacht 2013.

  This file is part of Sasalib.
  
  Sasalib is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  
  Sasalib is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with Sasalib.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <math.h>
#include "sasa.h"
#include "srp.h"

#ifndef PI
#define PI 3.14159265358979323846
#endif

#ifdef __GNUC__
#define __attrib_pure__ __attribute__((pure))
#else
#define __attrib_pure__
#endif

/** internal functions for S&R calculations **/
// calculation parameters (results stored in *sasa)
typedef struct {
    int i1,i2;
    int n_atoms;
    int n_points;
    const coord_t *xyz;
    const coord_t *srp;
    const double *r;
    double *sasa;
    char *spcount_0;
} sasa_sr_t;

#ifdef PTHREADS
static void sasa_sr_do_threads(int n_threads, sasa_sr_t sr);
static void *sasa_sr_thread(void *arg);
#endif 

static double sasa_sr_calc_atom(int i,const sasa_sr_t) __attrib_pure__;

/** internal functions for L&R calculations **/
//calculation parameters (results stored in *sasa)
typedef struct {
    int n_atoms;
    const double *radii;
    const coord_t *xyz;
    const int **nb;
    const int *nn;
    double delta;
    double min_z;
    double max_z;
    double *sasa;
} sasa_lr_t; 

#ifdef PTHREADS
static void sasa_lr_do_threads(int n_threads, sasa_lr_t);
static void *sasa_lr_thread(void *arg);
#endif

static void sasa_add_slice_area(double z, sasa_lr_t);

// the z argument is only really necessary for the debugging section
static void sasa_exposed_arcs(int n_slice, const double *x, const double *y, double z, 
                              const double *r, double *exposed_arc, 
                              const int **nb, const int *nn);
//does not necessarily leave a and b in a consistent state
static double sasa_sum_angles(int n_buried, double *a, double *b);

/** Calculate contacts, given coordinates and radii. The array nb will
    have a list of neighbors to each atom, nn will say how many
    neighbors each atom has. The arrays nn and nb should be of size
    n_atoms. The elements of n_atoms are dynamically allocated to be
    of size nn[i]. **/
static void sasa_get_contacts(int **nb, int *nn,
                              const coord_t *xyz, const double *radii);

int sasa_shrake_rupley(double *sasa,
                       const coord_t *xyz,
                       const double *r,
                       int n_points,
                       int n_threads)
{
    size_t n_atoms = coord_n(xyz);
    
    // Initialize test-points
    const double *srp_p = srp_get_points(n_points);
    if (srp_p == NULL) return 1;
    coord_t *srp = coord_new_linked(srp_p,n_points);

    char spcount_0[n_points];

    /* a reference zero array is prepared so that it won't need to be
       initialized for each atom */
    for (int k = 0; k < n_points; ++k) {
        spcount_0[k] = 0;
    }
    //store parameters and reference arrays
    sasa_sr_t sr = {.n_atoms = n_atoms, .n_points = n_points, 
                    .xyz = xyz, .srp = srp,
                    .r = r, .spcount_0 = spcount_0,
                    .sasa = sasa};

    //calculate SASA 
    if (n_threads > 1) {
#ifdef PTHREADS
        sasa_sr_do_threads(n_threads, sr);
#else 
        perror("Error: program compiled for single-threaded used, but multiple threads were requested.");
        exit(EXIT_FAILURE);
#endif
    }
    else {
        // don't want the overhead of generating threads if only one is used
        for (int i = 0; i < n_atoms; ++i) {
            sasa[i] = sasa_sr_calc_atom(i,sr);
        }
    }
    coord_free(srp);
    return 0;
}

#ifdef PTHREADS
static void sasa_sr_do_threads(int n_threads, sasa_sr_t sr)
{
    pthread_t thread[n_threads];
    sasa_sr_t srt[n_threads];
    int n_atoms = sr.n_atoms;
    int thread_block_size = n_atoms/n_threads;
    for (int t = 0; t < n_threads; ++t) {
	srt[t] = sr;
	srt[t].i1 = t*thread_block_size;
	if (t == n_threads-1) srt[t].i2 = n_atoms;
	else srt[t].i2 = (t+1)*thread_block_size;
	int res = pthread_create(&thread[t], NULL, sasa_sr_thread, (void *) &srt[t]);
	if (res) {
	    perror("Thread creation failed");
	    exit(EXIT_FAILURE);
	}
    }
    for (int t = 0; t < n_threads; ++t) {
	void *thread_result;
	int res = pthread_join(thread[t],&thread_result);
	if (res) {
	    perror("Thread join failed");
	    exit(EXIT_FAILURE);
	}
    }
}

static void *sasa_sr_thread(void* arg)
{
    sasa_sr_t sr = *((sasa_sr_t*) arg);
    int n = sr.i2-sr.i1;
    double *sasa = (double*)malloc(sizeof(double)*n);
    for (int i = 0; i < n; ++i) {
        sasa[i] = sasa_sr_calc_atom(i+sr.i1,sr);
    }
    // mutex should not be necessary, but might be safer to have one here?
    memcpy(&sr.sasa[sr.i1],sasa,sizeof(double)*n);
    free(sasa);
    pthread_exit(NULL);
}
#endif

static double sasa_sr_calc_atom(int i, const sasa_sr_t sr) {
    const int n_points = sr.n_points;
    const coord_t *xyz = sr.xyz;
    /* this array keeps track of which testpoints belonging to
       a certain atom overlap with other atoms */
    char spcount[n_points];
    const double ri = sr.r[i]+SASA_PROBE_RADIUS;
    const double *vi = coord_i(xyz,i);
    double xi = vi[0], yi = vi[1], zi = vi[2]; 
    const double *v = coord_all(xyz);

    /* testpoints for this atom */
    coord_t* tp_coord_ri = coord_copy(sr.srp);
    coord_scale(tp_coord_ri, ri);
    coord_translate(tp_coord_ri, vi);
    const double *tp = coord_all(tp_coord_ri);

    memcpy(spcount,sr.spcount_0,sizeof(char)*n_points);
    
    for (int j = 0; j < sr.n_atoms; ++j) {
        if (j == i) continue;
	const double rj = sr.r[j]+SASA_PROBE_RADIUS;
        const double cut2 = (ri+rj)*(ri+rj);

	/* this turns out to be the fastest way (by far) to access the
	   coordinates, probably because it's easier for the compiler
	   to optimize */
	double xj = v[3*j+0], yj = v[3*j+1], zj = v[3*j+2];
	double dx = xj-xi, dy = yj-yi, dz = zj-zi;
	if (dx*dx + dy*dy + dz*dz > cut2) continue;
        
	for (int k = 0; k < n_points; ++k) {
            if (spcount[k]) continue;
	    double dx = tp[3*k]-xj, dy = tp[3*k+1]-yj, dz = tp[3*k+2]-zj;
	    if (dx*dx + dy*dy + dz*dz < rj*rj) {
		spcount[k] = 1;
	    }
	    /* i.e. if |xyz[i]+ri*srp[k] - xyz[j]| <= rj we have an
               overlap. */
        }
    }
    coord_free(tp_coord_ri);
    int n_surface = 0;
    for (int k = 0; k < n_points; ++k) {
        if (!spcount[k]) ++n_surface;
    }
    return (4.0*PI*ri*ri*n_surface)/n_points;
}

int sasa_lee_richards(double *sasa,
                      const coord_t *xyz,
                      const double *atom_radii,
                      double delta,
                      int n_threads)
{
    /* Steps:
       Define slice range
       For each slice:
       1. Identify member atoms
       2. Calculate their radii in slice
       3. Calculate exposed arc-lengths for each atom
       Sum up arc-length*delta for each atom
    */
    size_t n_atoms = coord_n(xyz);

    // determine slice range and init radii and sasa arrays
    double max_z=-1e50, min_z=1e50;
    double max_r = 0;
    double radii[n_atoms];
    const double *v = coord_all(xyz);
    for (size_t i = 0; i < n_atoms; ++i) {
        radii[i] = atom_radii[i] + SASA_PROBE_RADIUS;
        double z = v[3*i+2], r = radii[i];
        max_z = z > max_z ? z : max_z;
        min_z = z < min_z ? z : min_z;
        sasa[i] = 0;
        max_r = r > max_r ? r : max_r;
    }
    min_z -= max_r;
    max_z += max_r;
    min_z += 0.5*delta;
 
    // determine which atoms are neighbours
    int *nb[n_atoms], nn[n_atoms];
    sasa_get_contacts((int**)nb, (int*)nn, xyz, radii);
    sasa_lr_t lr = {.n_atoms = n_atoms, .radii = radii, .xyz = xyz,
                    .nb = (const int**)nb, .nn = nn, .delta = delta, 
                    .min_z = min_z, .max_z = max_z, .sasa = sasa};
    
    if (n_threads > 1) {
#ifdef PTHREADS
        sasa_lr_do_threads(n_threads, lr);
#else
        perror("Error: program compiled for single-threaded used, but multiple threads were requested.");
        exit(EXIT_FAILURE);
#endif
    } else {
        // loop over slices
        for (double z = min_z; z < max_z; z += delta) {
            sasa_add_slice_area(z,lr);
        }
    }    
    for (int i = 0; i < n_atoms; ++i) free(nb[i]);
    return 0;
}

#ifdef PTHREADS
static void sasa_lr_do_threads(int n_threads, sasa_lr_t lr) 
{
    double *t_sasa[n_threads];
    pthread_t thread[n_threads];
    sasa_lr_t lrt[n_threads];
    const double max_z = lr.max_z, min_z = lr.min_z, delta = lr.delta;
    int n_slices = (int)ceil((max_z-min_z)/delta);
    int n_perthread = n_slices/n_threads;
    for (int t = 0; t < n_threads; ++t) {
        t_sasa[t] = (double*)malloc(sizeof(double)*lr.n_atoms);
        for (int i = 0; i < lr.n_atoms; ++i) t_sasa[t][i] = 0;
        lrt[t] = lr;
        lrt[t].sasa = t_sasa[t];
        lrt[t].min_z = min_z + t*n_perthread*delta;
        if (t < n_threads - 1) {
            lrt[t].max_z = lrt[t].min_z + n_perthread*delta;
        } else {
            lrt[t].max_z = max_z;
        }
        int res = pthread_create(&thread[t], NULL, sasa_lr_thread, (void *) &lrt[t]);
        if (res) {
            perror("Thread creation failed");
            exit(EXIT_FAILURE);
        }
    }
    for (int t = 0; t < n_threads; ++t) {
        void *thread_result;
        int res = pthread_join(thread[t],&thread_result);
        if (res) {
            perror("Thread join failed");
            exit(EXIT_FAILURE);
        }
    }
    
    for (int t = 0; t < n_threads; ++t) {
        for (int i = 0; i < lr.n_atoms; ++i) {
            lr.sasa[i] += t_sasa[t][i];
        }
        free(t_sasa[t]);
    }
}

static void *sasa_lr_thread(void *arg)
{
    sasa_lr_t lr = *((sasa_lr_t*) arg);
    for (double z = lr.min_z; z < lr.max_z; z += lr.delta) {
        sasa_add_slice_area(z, lr);
    }
    pthread_exit(NULL);
}
#endif

static void sasa_add_slice_area(double z, sasa_lr_t lr)
{
    int n_atoms = lr.n_atoms;
    double x[n_atoms], y[n_atoms], r[n_atoms], DR[n_atoms];
    double delta = lr.delta;
    int n_slice = 0;
    double exposed_arc[n_atoms];
    int idx[n_atoms], xdi[n_atoms], in_slice[n_atoms], nn_slice[n_atoms], *nb_slice[n_atoms];
    const double* v = coord_all(lr.xyz);

    // locate atoms in each slice and do some initialization
    for (size_t i = 0; i < n_atoms; ++i) {
        double ri = lr.radii[i];
        double d = fabs(v[3*i+2]-z);
        if (d < ri) {
            x[n_slice] = v[i*3]; y[n_slice] = v[i*3+1];
            r[n_slice] = sqrt(ri*ri-d*d);
            //multiplicative factor when arcs are summed up later (according to L&R paper)
            DR[n_slice] = ri/r[n_slice]*(delta/2. +
                                         (delta/2. < ri-d ? delta/2. : ri-d));
            idx[n_slice] = i;
            xdi[i] = n_slice;
            ++n_slice;
            in_slice[i] = 1;
        } else {
            in_slice[i] = 0;
        }
    }
    for (int i = 0; i < n_slice; ++i) { 
        nn_slice[i] = 0;
        nb_slice[i] = NULL;
        exposed_arc[i] = 0;
    }
    
    for (int i = 0; i < n_slice; ++i) {
        int i2 = idx[i];
        int j2;
        for (int j = 0; j < lr.nn[i2]; ++j) {
            if (in_slice[j2 = lr.nb[i2][j]]) {
                ++nn_slice[i];
                nb_slice[i] = realloc(nb_slice[i],sizeof(int)*nn_slice[i]);
                nb_slice[i][nn_slice[i]-1] = xdi[j2];
            }
        }
    }
    
    //find exposed arcs
    sasa_exposed_arcs(n_slice, x, y, z, r, exposed_arc, (const int**)nb_slice, nn_slice);
    
    // calculate contribution to each atom's SASA from the present slice
    for (int i = 0; i < n_slice; ++i) {
        lr.sasa[idx[i]] += exposed_arc[i]*r[i]*DR[i];
    }
    for (int i = 0; i < n_slice; ++i) {
        free(nb_slice[i]);
    }
}

static void sasa_exposed_arcs(int n_slice, const double *x, const double *y, double z, const double *r,
                              double *exposed_arc, const int **nb, const int *nn)
{
    int is_completely_buried[n_slice]; // keep track of completely buried circles
    for (int i = 0; i < n_slice; ++i) is_completely_buried[i] = 0;
    //loop over atoms in slice
    for (int i = 0; i < n_slice; ++i) {
        double ri = r[i], a[n_slice], b[n_slice];
        int n_buried = 0;
        exposed_arc[i] = 0;
        if (is_completely_buried[i]) {
            continue;
        }
        // loop over neighbors in slice
        for (int ni = 0; ni < nn[i]; ++ni) {
            int j = nb[i][ni];
            assert (i != j);
            double rj = r[j], xij = x[j]-x[i], yij = y[j]-y[i];
            double d = sqrt(xij*xij+yij*yij);
            // reasons to skip calculation
            if (d >= ri + rj) continue;     // atoms aren't in contact
            if (d + ri < rj) { // circle i is completely inside j
                is_completely_buried[i] = 1; 
                break;
            } 
            if (d + rj < ri) { // circle j is completely inside i
                is_completely_buried[j] = 1;
                continue;
            } 
            
            // half the arclength occluded from circle i due to verlap with circle j
            double alpha = acos ((ri*ri + d*d - rj*rj)/(2.0*ri*d));
            // the polar coordinates angle of the vector connecting i and j
            double beta = atan2 (yij,xij);
            
            a[n_buried] = alpha;
            b[n_buried] = beta;
            
            ++n_buried;
        }
        if (is_completely_buried[i] == 0) 
            exposed_arc[i] = sasa_sum_angles(n_buried,a,b);
        
#ifdef DEBUG
        if (is_completely_buried[i] == 0) {
            //exposed_arc[i] = 0;
            for (double c = 0; c < 2*PI; c += PI/45.0) {
                int is_exp = 1;
                for (int i = 0; i < n_buried; ++i) {
                    if ((c > b[i]-a[i] && c < b[i]+a[i]) ||
                        (c - 2*PI > b[i]-a[i] && c - 2*PI < b[i]+a[i]) ||
                        (c + 2*PI > b[i]-a[i] && c + 2*PI < b[i]+a[i])) { 
                        is_exp = 0; break; 
                    }
                }
                // print the arcs used in calculation
                if (is_exp) printf("%6.2f %6.2f %6.2f %7.5f\n",
                                   x[i]+ri*cos(c),y[i]+ri*sin(c),z,c);
                //if (is_exp) exposed_arc[i] += PI/45.0;
            }
            printf("\n");
        }
#endif
    }
}

static double sasa_sum_angles(int n_buried, double *a, double *b)
{
    int excluded[n_buried], n_exc = 0, n_overlap = 0;
    for (int i = 0; i < n_buried; ++i)  {
        excluded[i] = 0;
        assert(a[i] > 0);
    }
    for (int i = 0; i < n_buried; ++i) {
        if (excluded[i]) continue;
        for (int j = 0; j < n_buried; ++j) {
            if (excluded[j]) continue;
            if (i == j) continue;
            
            //check for overlap
            double bi = b[i], ai = a[i]; //will be updating throughout the loop
            double bj = b[j], aj = a[j];
            double d;
            for (;;) {
                d = bj - bi;
                if (d > PI) bj -= 2*PI;
                else if (d < -PI) bj += 2*PI;
                else break;
            }
            if (fabs(d) > ai+aj) continue;
            ++n_overlap;
            
            //calculate new joint interval
            double inf_i = bi-ai, inf_j = bj-aj;
            double sup_i = bi+ai, sup_j = bj+aj;
            double inf = inf_i < inf_j ? inf_i : inf_j;
            double sup = sup_i > sup_j ? sup_i : sup_j;
            b[i] = (inf + sup)/2.0;
            a[i] = (sup - inf)/2.0;
            if (a[i] > PI) return 0;
            if (b[i] > PI) b[i] -= 2*PI;
            if (b[i] < -PI) b[i] += 2*PI;
            
            a[j] = 0; // the j:th interval should be ignored 
            excluded[j] = 1;
            if (++n_exc == n_buried-1) break;
        }
        if (n_exc == n_buried-1) break; // means everything's been counted
    }
    
    // recursion until no overlapping intervals
    if (n_overlap) {
        double b2[n_buried], a2[n_buried];
        int n = 0;
        for (int i = 0; i < n_buried; ++i) {
            if (excluded[i] == 0) {
                b2[n] = b[i];
                a2[n] = a[i];
                ++n;
            }
        }
        return sasa_sum_angles(n,a2,b2);
    }
    // else return angle
    double buried_angle = 0;
    for (int i = 0; i < n_buried; ++i) {
        buried_angle += 2.0*a[i];
    }
    return 2*PI - buried_angle;
}

static void sasa_get_contacts(int **nb, int *nn, 
                              const coord_t *xyz, const double *radii)
{
    /* For low resolution L&R this function is the bottleneck in
       speed. Will also depend on number of atoms. */
    size_t n_atoms = coord_n(xyz);
    for (int i = 0; i < n_atoms; ++i) {
        nn[i] = 0;
        nb[i] = NULL;
    }
    const double *v = coord_all(xyz);

    for (int i = 0; i < n_atoms; ++i) {
        double ri = radii[i];
        double xi = v[i*3], yi = v[i*3+1], zi = v[i*3+2];
        for (int j = i+1; j < n_atoms; ++j) {
            double rj = radii[j];
            double cut2 = (ri+rj)*(ri+rj);
            
            /* most pairs of atoms will be far away from each other on
               at least one axis, the following improves speed
               significantly for large proteins */	    
            double xj = v[j*3], yj = v[j*3+1], zj = v[j*3+2];
            if ((xj-xi)*(xj-xi) > cut2 ||
                (yj-yi)*(yj-yi) > cut2 ||
                (zj-zi)*(zj-zi) > cut2) {
                continue;
            }
	    double dx = xj-xi, dy = yj-yi, dz = zj-zi;
            if (dx*dx + dy*dy + dz*dz < cut2) {
                ++nn[i]; ++nn[j];
                nb[i] = realloc(nb[i],sizeof(int)*nn[i]);
                nb[j] = realloc(nb[j],sizeof(int)*nn[j]);
                nb[i][nn[i]-1] = j;
                nb[j][nn[j]-1] = i;
            }
        }
    }
}

