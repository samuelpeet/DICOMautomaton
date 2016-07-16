//KineticModel_1Compartment2Input_5Param_LinearInterp_LevenbergMarquardt.h.

#pragma once

#include <limits>
#include <memory>

#include "YgorMath.h"

#include "KineticModel_1Compartment2Input_5Param_LinearInterp_Structs.h"


// This routine can be used to evaluate a model (using the parameters in 'state') at given time.
void
Evaluate_Model( const KineticModel_1Compartment2Input_5Param_LinearInterp_Parameters &state,
                const double t,
                KineticModel_1Compartment2Input_5Param_LinearInterp_Results &res);
 

// This routine fits a pharmacokinetic model to the observed liver perfusion data using a 
// direct linear interpolation approach.
//
// This routine fits all 5 model free parameters (k1A, tauA, k1V, tauV, k2) numerically.
//
struct KineticModel_1Compartment2Input_5Param_LinearInterp_Parameters
Optimize_LevenbergMarquardt_5Param(KineticModel_1Compartment2Input_5Param_LinearInterp_Parameters state);


// This routine fits a pharmacokinetic model to the observed liver perfusion data using a 
// direct linear interpolation approach.
//
// This routine fits only 3 model free parameters (k1A, k1V, k2) numerically. The neglected
// parameters (tauA, tauV) are kept at 0.0.
//
struct KineticModel_1Compartment2Input_5Param_LinearInterp_Parameters
Optimize_LevenbergMarquardt_3Param(KineticModel_1Compartment2Input_5Param_LinearInterp_Parameters state);


