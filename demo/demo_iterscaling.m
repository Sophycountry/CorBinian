% Demo script to showcase code for fast iterative scaling with for 
% fitting K-pairwise maximum entropy models for multivariate binary data. 

% Fast iterative scaling for regularized K-pairwise maximum entropy models 
% based on pairwise Gibbs sampling with Rao-Blackwellized estimators for 
% first and second moments of the distribution.

% Instructions:
%  - Change to the code directory and run
%  - The script will take you through the functions .
%  - After each step, the script will pause. 
%  - To continue, hit any button.
%  - Read along in the demo file to follow what's happening.

clear all

% Simulation setup
%--------------------------------------------------------------------------
d=15;                  % data dimensionality
nSamplesData  = 10000; % draw from ground-truth parameters
nSamplesEval  = 10000; % draw from paramter estimates for comparison
burnIn        =  1000;

h=0.25*randn(d,1)-3.5;           % generate random bias terms h
J= 0.45*(randn(d)); J=triu(J,1); % generate interaction terms J 
lambda=hJ2lambda(h,J);           % vectorize
V = [0; linspace(3.5, -1, d)'];
lambdaTrue = [lambda;V];         % append population count terms V

%% generate training data
%--------------------------------------------------------------------------
% Initialize training data-generating MCMC chain with a sample drawn from
% a nested model (only h = lamdbdaTrue(1:d), i.e. no J, no V)
EX = exp(lambdaTrue(1:d))./(1+exp(lambdaTrue(1:d))); 
x0 = double(rand(d,1)<EX);                           

disp('Generating training data')
[mfxTrain,~,~] = maxEnt_gibbs_pair_C(nSamplesData, burnIn, ...
                                     lambdaTrue, x0);

pause;
 
%% train model
%--------------------------------------------------------------------------
disp('Fitting maxEnt model')

% initialize optimization with independent model
EX = mfxTrain(1:d);
lambdaInd = zeros(size(lambdaTrue)); 
lambdaInd(1:d) = log(EX./(1-EX));
lambdaInd(lambdaInd==Inf) =   1000; % catch cases EX(k)=0 resp. EX(k)=1
lambdaInd(lambdaInd==-Inf) = -1000; % 
fitoptions.lambda0 = lambdaInd;
clear EX

          
hJV = ones(3,1); % booleans giving whether to include h, J and/or V 
ifbwVK = true;   % flag for blockwise update of parameter terms V

ifSave = false;  % whether to store results on disk
fname = '';      % filename for storing results on disk


fitoptions.regular = 'l1';
beta = 0.00001*ones(d*(d+1)/2 + d+1,1); % strength of l1 regularizer
fitoptions.nRestart = 1;
fitoptions.modelFit = 'ising_count_l_0';

fitoptions.nSamples = 100;    
fitoptions.burnIn   =  10;
fitoptions.maxIter  = 1000;
fitoptions.maxInnerIter = 1;        
eps = [0.05, 0.05, 0.1]; % convergence criteria, empty loads defaults

% create sequence of increasing MCMC chain lengths for each update step
a = fitoptions.nSamples; % initial MCMC chain lengths
tau = 400;               % update steps over which chain length doubles
fitoptions.nSamples = [0;round(a * 2.^((1:fitoptions.maxIter)'/tau))];
fitoptions.nSamples = floor(fitoptions.nSamples/100)*100;          

disp('- starting iterative scaling')
[lambdaHat, fitDiagnostics] = iterScaling(mfxTrain, fitoptions, ...
                          beta, eps, fname, ifSave, hJV, ifbwVK);

%pause;

%% validate model
%--------------------------------------------------------------------------

% for small systems, we can compute P( X | lambdaTrue ) analytically
if d < 20
 [features,description,x]=setup_features_maxent(d,'ising_count_l_0');
 [~,~,Ptrue, ~]=logPMaxEnt(features,lambdaTrue);
 EX = sum(bsxfun(@times, x', Ptrue'),2);
 description(isnan(description)) = d+1;
 x1 = x; 
 x1(:,end+1) = 1; 
 EXX = sum(bsxfun(@times, (x1(:,description(1,d+1:d*(d+1)/2))...
                        .* x1(:,description(2,d+1:d*(d+1)/2)))',Ptrue'),2);
 EK = zeros(length(V),1);
 for k = 1:length(EK)
  EK(k) = sum((sum(x,2)==(k-1)) .* Ptrue);
 end
 mfxEval = [EX(:);EXX(:);EK(:)];
 clear x1 EX EXX EK description features Ptrue 

else % for large systems, sample (long) MCMC chain

 disp('Generating data from model fit')
 [mfxEval,~,~] = maxEnt_gibbs_pair_C(nSamplesEval, burnIn, ...
                                     lambdaHat, x0);
    
end % if d < 20  
                    
% visualize results

figure; 
subplot(131)
plot(mfxTrain(1:d), mfxEval(1:d), 'k.');
title('first moments')
xlabel('est.')
ylabel('data')
axis square

subplot(132)
plot(mfxTrain(d+1:end-d-1), mfxEval(d+1:end-d-1), 'k.')
title('second moments')
xlabel('est.')
ylabel('data')
axis square

subplot(133)
plot(mfxTrain(end-d:end), 'ko-')
hold on
plot(mfxEval(end-d:end), 'ro-')
hold off
xlabel('population count K')
ylabel('probability P(K)')
legend({'data', 'est.'})
title('population counts')
