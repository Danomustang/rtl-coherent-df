#include <complex.h>
#include <fftw3.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include "configuration.h"
#include "correlate.h"
#include "df.h"

#define PIf 3.14159265f

static int nreceivers=0, fft1n=0, covarsize=0;


static fftwf_complex *fft1in, *fft1out;
static float complex *covar;
static float *fft1win;
static fftwf_plan fft1plan;

int corr_init() {
	int i, arraysize;
	nreceivers = conf.nreceivers;
	
	fft1n = conf.cor_fft;
	
	arraysize = nreceivers * fft1n;
	fft1in  = fftwf_malloc(arraysize * sizeof(*fft1in));
	fft1out = fftwf_malloc(arraysize * sizeof(*fft1out));
	for(i = 0; i < arraysize; i++)
		fft1in[i] = fft1out[i] = 0;
		
	fft1win = fftwf_malloc(fft1n * sizeof(*fft1win));
	for(i = 0; i < fft1n; i++) {
		fft1win[i] = sinf(PIf * (i + 0.5f) / fft1n);
	}
	
	covarsize = nreceivers * nreceivers * fft1n;
	covar = fftwf_malloc(covarsize * sizeof(*covar));

	fft1plan = fftwf_plan_many_dft(
		1, &fft1n, nreceivers,
		fft1in,  NULL, 1, fft1n,
		fft1out, NULL, 1, fft1n,
		FFTW_FORWARD, FFTW_ESTIMATE);

	return 0;
}


int corr_block(int blocksize, csample_t **buffers, float *fracdiffs, float *phasediffs) {
	int ri, ri2, ci, i;
	int si;
	int windowstep = fft1n / 2; /* 50% overlap */
	for(ci = 0; ci < covarsize; ci++) covar[ci] = 0;
	
	/* calculate covariance matrix for each frequency */
	for(si = 0; si < blocksize-fft1n; si += windowstep) {
		for(ri = 0; ri < nreceivers; ri++) {
			csample_t *buf = buffers[ri] + si;
			fftwf_complex *fftinbuf = fft1in + ri * fft1n;
			for(i = 0; i < fft1n; i++) {
				fftinbuf[i] = ((buf[i][0] + I*buf[i][1]) - (127.4f+127.4f*I)) * fft1win[i];
			}
		}
		fftwf_execute(fft1plan);
		for(ri = 0; ri < nreceivers; ri++) {
			/* phase slope to approximate fractional delay: */
			float fds = -2*PIf * (fracdiffs[ri] + conf.calibdelay[ri] * conf.sample_rate) / fft1n;
			/* optimization: don't calculate cexpf(I * fds * i) inside the loop */
			float complex fdsc = cexpf(I * fds);
			/* include correction for most negative frequency in pd first */
			float complex pd = cexpf(I * (phasediffs[ri] - 2.f*M_PI * conf.calibdelay[ri] * conf.center_freq - fds*fft1n/2));
			
			/* negative frequencies first */
			for(i = fft1n/2; i < fft1n; i++) {
				fft1out[fft1n*ri + i] *= pd;
				pd *= fdsc;
			}
			/* positive frequencies */
			for(i = 0; i < fft1n/2; i++) {
				fft1out[fft1n*ri + i] *= pd;
				pd *= fdsc;
			}
		}
		ci = 0;
		for(i = 0; i < fft1n; i++) {
			for(ri = 0; ri < nreceivers; ri++) {
				for(ri2 = 0; ri2 < nreceivers; ri2++) {
					covar[ci] += fft1out[fft1n*ri + i] * conjf(fft1out[fft1n*ri2 + i]);
					ci++;
				}
			}
		}
	}

	if(!conf.calibrationmode) {
		df_block(covar);
	} else {
		/* calibration mode:
		find the frequency with strongest correlation
		and print its covariance matrix */
		float cvabssumbest = 0;
		int cvabssumbesti = 0;
		/* find the frequency with strongest correlation (for testing) */
		ci = 0;
		for(i = 0; i < fft1n; i++) {
			float cvabssum = 0;
			//if(i == 31) continue;
			for(ri = 0; ri < nreceivers; ri++) {
				for(ri2 = 0; ri2 < nreceivers; ri2++) {
					if(ri != ri2) cvabssum += cabsf(covar[ci]);
					ci++;
				}
			}
			if(cvabssum > cvabssumbest) {
				cvabssumbest = cvabssum;
				cvabssumbesti = i;
			}
		}
		/* print covariance matrix for that frequency */
		printf("\033[32m%E %d \033[1m\n", cvabssumbest, cvabssumbesti);

		const int covarp = nreceivers*nreceivers;
		ci = covarp * cvabssumbesti;
		for(ri = 0; ri < nreceivers; ri++) {
			for(ri2 = 0; ri2 < nreceivers; ri2++) {
				int mag_dB = 5.0 * log10f(crealf(covar[ci])*crealf(covar[ci]) + cimagf(covar[ci])*cimagf(covar[ci]));
				int phase_deg = 57.2957795f*cargf(covar[ci]);
				printf("%-4d %-4d | ", mag_dB, phase_deg);
				ci++;
			}
			printf("    \n");
		}
		printf("\033[0m\n\033[H");
		//printf("\033[0m\n\f");
		#if 0
		/* print the matrix in fifo */
		ci = covarp * cvabssumbesti;
		FILE *fi = fopen("fifo", "w");
		if(fi) {
			for(ri = 0; ri < nreceivers; ri++) {
				for(ri2 = 0; ri2 < nreceivers; ri2++) {
					fprintf(fi, "%f%+fj\n", creal(covar[ci]), cimag(covar[ci]));
					ci++;
				}
			}
			fclose(fi);
		}
		#endif
	}
	return 0;
}


int corr_exit() {
	/* TODO: free everything */
	return 0;
}

