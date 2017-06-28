/*=================================================================
 * This is a MEX-file for MATLAB.  
 * All rights reserved.
 *=================================================================*/

/* pairwise Gibbs sampler MEX file */

#include <algorithm>   // for random_shuffle
#include "time.h"
#include "math.h"      // for exp()
#include "mex.h"   
//#include <vector>	   // for std::vector
//#include "random"    // for std::default_random_enginge


void mexFunction(int nlhs,mxArray *plhs[],int nrhs,const mxArray *prhs[]) {
    
/* 1. variable declarations */
    
    const int nSamples    = mxGetScalar(prhs[0]); // number of samples 
    const int burnIn      = mxGetScalar(prhs[1]); // length burnin phase
    const int d           = mxGetScalar(prhs[2]); // data dimensionality
    	  double * x0          = mxGetPr(prhs[3]); // initial chain segment
    	  double * pairs0      =  mxGetPr(prhs[4]); // look-up table #1
    	  double * fm0         =  mxGetPr(prhs[6]); // look-up table #2
    const double * h           = mxGetPr(prhs[7]); // single-cell weights
    const double * J           = mxGetPr(prhs[8]); // cell-pair weights
    const double * L           = mxGetPr(prhs[9]); // population weights
    
    int numAll  = d*(d+3)/2+1;	// number of features for (h,J,V(K)) model
    int numPair = d*(d-1)/2;    // number of pairs between d variables 
	int numIsing = numPair+d;   // number of features for Ising model 
   	 
    int * pairs            = new int[2*numPair];
    int * fm               = new int[d*(d-1)];
    
    // The following variables are of dynamic size (=> quite the pain in C)
    mxArray *outVar = mxCreateDoubleMatrix(numAll, 1, mxREAL);
    double * xSampled = mxGetPr(outVar);
//     double * xTmp = malloc(numAll * sizeof(double)); // intermediate storage (results after each sweep)
//     double * pc   = malloc(numAll * sizeof(double)); // short-term storage (results within each sweep)
//     bool   * xc   = malloc(d * sizeof(bool)); // current chain member   
//     int    * ks   = malloc(numPair * sizeof(int)); // index vector to keep track of pairs in a sweep     
    double * xTmp = new double[numAll]; // intermediate storage (results after each sweep)
    double * pc   = new double[numAll]; // short-term storage (results within each sweep)
    bool   * xc   = new bool[numAll]; // current chain member   
    
    int    * ks   = new int[numPair]; // index vector to keep track of pairs in a sweep 
    int* ksfirst = ks;           // pointers to first and last element of
    int* kslast  = (ks+numPair); // index array
    
    int idl=0;					// current population activity count K
    
    int k,l;					// indices for current pair of variables 
    int kl;                     // index for state of current variable pair 
    int dmok, dmol, fmidx;      // convenience indexes for entries of J
    double sk, sl, slk;			// summary variable for effects from J
    double L0, L1, L2;		    // variables for effects from L

    double E;  // short-term storage for energy 
    double Ec, E2c; // intermediate storage for (squared) energy (variance)
    double ETmp  = 0;  // intermediate storages 
    double E2Tmp = 0;  // (will be updated after each sweep) 
    double ESampled;        // long-term storages (will be
    double E2Sampled;       // updated each 100 sweeps or so)
    double pE[4]; // partial energies for all configurations of x(k),x(l)
    
    double p[4]; // probabilities for bivariate distribution over x(k),x(l)
    double sump, maxpE; // normalizer and maximum entry of partial energies
    
    
    double rnd;	// random variable to decide new entries for x(k), x(l)

    int idx; // all-purpose index for iterating through vector entries
    int i,j; // indexes for outer and inner Gibbs loops, respectively
    
/* 2. Initialize variables where needed */
    
    srand((unsigned)time(NULL));              // seed random stream

    for (idx=0; idx<numPair; idx++) {
        ks[idx] = idx; // default order of pairs: counted from 1 to numPair
    }
    for (idx=0; idx<d; idx++) {
    	if (*(x0+idx)==1) {
            xc[idx] = true; // current chain segment x
    		idl+=1;	        // population activity count
        }
        else {
            xc[idx] = false;
    	}
    }
    
    
    for (idx=0; idx<numAll; idx++) {
        xTmp[idx] = 0;  // intermediate storage for Rao-Blackwell variables
    }        
    
    for (idx=0; idx<2*numPair; idx++) {
        *(pairs+idx) = (int) *(pairs0+idx);  // 
    }       
    
    for (idx=0; idx<d*(d-1); idx++) {
        *(fm+idx) = (int) *(fm0+idx);  // 
    }       
    
    for (idx=0; idx<numAll; idx++) {
        xTmp[idx] = 0;  // intermediate storage for Rao-Blackwell variables
    }       
    
    ESampled = 0;
    E2Sampled = 0;
    
/* 3. Start MCMC chain */
 
	/* FOR EACH OF THE IN TOTAL nSamples + burnIn MANY SWEEPS... */
    for (i = -burnIn+1; i < nSamples+1; i++) {

    	std::random_shuffle(ksfirst, kslast); // shuffle order for current sweep
        
        for (idx=0; idx < numAll; idx++) {
        	pc[idx] = 0;	// reset current results for expected values
        }
        Ec = 0;     // reset current results for terms needed to 
        E2c = 0;    // estimate the variance of energy
        
        /* ... VISIT EACH POSSIBLE PAIR OF VARIABLES AND UPDATE THEM */
        for (j = 0; j < numPair; j++) {
     
            k = *(pairs+ks[j]);			// choose current pair of 
         	l = *(pairs+ks[j]+numPair);	// variables to work on
            
            kl = 0; 
            if (xc[k]) { //
                idl--;   // momentarily assume both variables
                kl = 2;  // to equal 0
            }            //
            if (xc[l]) { // We'll add the true values at the 
                idl--;   // end of the loop iteration.
                kl++;    //
            }            //
            
            /* compute probabilities for bivariate distribution over x(k),
             * x(l) by inspecting all four possible configurations */
    		xc[k] = true;
    		xc[l] = false;
    		sk  = 0; // storage for effects from J for case x(k)=1, x(l)=0
            dmok  = (d-1)*k; // column index in vectorized look-up table fm
    		for (idx=0; idx < d-1; idx++) {
                fmidx = *(fm+idx+dmok); // current index within pairs 
                if (xc[(int) *(pairs+fmidx)] && xc[(int) *(pairs+fmidx+numPair)]) {
                    sk += J[fmidx];
                }
            }
    		xc[k] = false;
    		xc[l] = true;
    		sl  = 0; // storage for effects from J for case x(k)=0, x(l)=1
            dmol  = (d-1)*l; // column index in vectorized look-up table fm
    		for (idx=0; idx < d-1; idx++) {
                fmidx = *(fm+idx+dmol); // current index within pairs 
                if (xc[(int) *(pairs+fmidx)] && xc[(int) *(pairs+fmidx+numPair)]) {
                    sl += J[fmidx];
                    
                }
            }
    		xc[k] = true; // if both x(k),x(l)=1, we need to add a few more
    		slk = 0;      // entries of J that will be stored in slk
    		for (idx=0; idx < d-1; idx++) {
                fmidx = *(fm+idx+dmok); // current index within pairs 
                if (xc[(int) *(pairs+fmidx)] && xc[(int) *(pairs+fmidx+numPair)]) {
                    slk += J[fmidx];
                }
            }
            L0 = *(L+idl);   // retrieve entries of L corresponding
    		L1 = *(L+idl+1); // to 0,1 or 2 out of the variables
    		L2 = *(L+idl+2); // x(k) and x(l) being equal to 1

    		/* finish computing bivariate Bernoulli probabilities */
    		pE[0]= (                          L0); // x(k) = 0, x(l) = 0
    		pE[1]= (       h[l]       + sl  + L1); // x(k) = 0, x(l) = 1
    		pE[2]= (h[k]        + sk        + L1); // x(k) = 1, x(l) = 0
    		pE[3]= (h[k] + h[l] + slk + sl  + L2); // x(k) = 1, x(l) = 1

            E -= pE[kl]; // subtract partial energy of the old state
            
            maxpE = 0; // maximum of pE[0],...,pE[3]
            for (idx=0; idx<4; idx++) {
             if (pE[idx]>maxpE) {
                 maxpE = pE[idx];
             }
            }
            for (idx=0; idx<4; idx++) { // subtract maxp to hopefully 
             p[idx] = exp(pE[idx]-maxpE); // avoid numerical overflows
            }
    		sump = p[0]+p[1]+p[2]+p[3];
            p[0] /= sump;									//
            p[1] /= sump;									// normalize
            p[2] /= sump;									//
            p[3] /= sump;									//
            
            /* draw new pair of variables from bivariate Bernoulli */
            rnd = (rand()+1.0)/(RAND_MAX+1.0); // c99-style uniform distr.
            if (rnd>(p[2]+p[3])) { //
                xc[k] = false;     // 
                kl = 0;            // we last visited the configuration 
            }                      // x(k)=x(l)=1, now just need to flip
            else {                 // x(k),x(l) to zero if necessary. 
                idl++;             //
                kl = 2; 
            }
            if ((p[2] > rnd) || (rnd > (1 - p[0]))) {
                xc[l] = false;
            }
            else {
                idl++;
                kl += 1;
            }

            /* update and collect energy for outputting the variance */            
            E += pE[kl]; // update energy with changes from the new state
            Ec  += E;
            E2c += E*E;           
            
            /* collect results (no Rao-Blackwell) */
            if (xc[k]) {
                pc[k]++;
            }
            if (xc[l]) {
                pc[l]++;
            }
            if (xc[k]&&xc[l]) {
                pc[d+ks[j]]++;
            }
        	pc[numIsing+idl]++;
            
                            
        } // end for j = 0:numPair

        /* normalize results by number of visits */
        for (idx=0; idx<d; idx++) {
        	pc[idx] /= (d-1);
            pc[numIsing+idx] /= numPair;
        }
        pc[numAll-1] /= numPair; // final entry for L (d+1 entries!)
        Ec  /= numPair; // summed energy also needs to be normalized
        E2c /= numPair; 
        
        /* shove results to mid-term storage and final outputs */
	if (i > 0) {          //* after burn-in phase is over:
        	for (idx =0; idx<numAll; idx++) {// copy into mid-term storage
        		xTmp[idx] += pc[idx]; // adds up results from 100 sweeps 
            }                        // before passing on to xSampled
            ETmp += Ec;           
            E2Tmp += E2c;
            
        	if ((i % 100) == 0) {    // every hundred sweeps ...
            	for (idx = 0; idx<numAll; idx++) { // move to final results 
            		xSampled[idx] = ((i-100)*xSampled[idx] + xTmp[idx])/i;	
            		xTmp[idx] = 0; // (normalize regularly to avoid 
            	}                  //  numerical overflows)
                ESampled = ((i-100)*ESampled + ETmp)/i;
                ETmp = 0; 
                E2Sampled = ((i-100)*E2Sampled + E2Tmp)/i;
                E2Tmp = 0;                
                //mexPrintf("\nCurrent sample is # %d ", i);   
                //mexPrintf(" out of %d .", nSamples);   
                
                /* re-initialize energy from current chain element */
                E = *(L+idl);    // first add entry for population activity count
                for (k=0; k<d; k++) { // now add entries for h, J
                    if (*(xc+k)) {
                        E += h[k];      // entries for h
                    }
                    for (l=0; l<d-k-1; l++) { // borrowing index i for the moment...
                        if (*(xc+l+1+k) & *(xc+k)) {
                            E += J[ k*d - (k*(k+1))/2 + l];      // entries for J
                        }
                    }
                }
        	}
        }
        //else { // give status reports also during burn-in phase
        //    if (((i+burnIn) % 100) == 0) {
                //mexPrintf("\nAt burnin step # %d ", (i+burnIn));   
                //mexPrintf(" out of %d .", burnIn);               
         //   }
        //}
    } // end for i = -burnIn:nSamples 
    
 plhs[0] = outVar;
 if (nlhs > 1) {
    mxArray *outX = mxCreateDoubleMatrix(d, 1, mxREAL);
    double * xOut = mxGetPr(outX);
    for (idx=0; idx<d; idx++) {
        xOut[idx]=0;
        if (xc[idx]) {
            xOut[idx] = 1;
        }
    }
    plhs[1] = outX;    
 }
 if (nlhs > 2) {
    mxArray *outE = mxCreateDoubleMatrix(2, 1, mxREAL);
    double * EOut = mxGetPr(outE);
    EOut[0] = ESampled;
    EOut[1] = E2Sampled;
    plhs[2] = outE;    
 }
 delete [] pairs;
 delete [] fm;
 delete [] xTmp;
 delete [] pc;
 delete [] xc;
 delete [] ks;
 
 //plhs[2] = outE;  
}    

