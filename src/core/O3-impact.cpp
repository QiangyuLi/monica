/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
Authors:
Tommaso Stella <tommaso.stella@zalf.de>
Michael Berg <michael.berg@zalf.de>

Maintainers:
Currently maintained by the authors.

This file is part of the MONICA model.
Copyright (C) Leibniz Centre for Agricultural Landscape Research (ZALF)
*/

#include  "O3-impact.h"
#include <tuple>
#define _USE_MATH_DEFINES
#include <math.h>
#include <algorithm>

using namespace O3impact;

//from Ewert and Porter, 2000. Global Change Biology, 6(7), 735-750
double O3_uptake(double O3a, double gsc, double f_WS)
{
	//O3_up should be nmol m - 2 s - 1, set input accordingly
	double fDO3 = 0.93; //ratio of diffusion rates for O3 and CO2
	return O3a * gsc * f_WS * fDO3;
}

double hourly_O3_reduction_Ac(double O3_up, double gamma1, double gamma2)
{
	//mind the units : O3_up should be nmol m - 2 s - 1
	double fO3s_h = 1.0;

	if (O3_up > gamma1 / gamma2 && O3_up < (1.0 + gamma1) / gamma2)
	{
		fO3s_h = 1.0 + gamma1 - gamma2 * O3_up;
	}
	else if (O3_up > (1.0 + gamma1) / gamma2)
	{
		fO3s_h = 0;
	}
	
	return fO3s_h;
}

double cumulative_O3_reduction_Ac(double fO3s_h_arr[], double rO3s, int h)
{
	double fO3s_d = 1.0;
	
	if (h == 0)
	{
		fO3s_d = fO3s_h_arr[h] * rO3s;
	}
	else
	{
		fO3s_d = fO3s_h_arr[h] * fO3s_h_arr[h - 1];
	}

	return fO3s_d;
}

double O3_damage_recovery(double fO3s_d, double fLA)
{
	double rO3s = fO3s_d + (1.0 - fO3s_d) * fLA;
	return rO3s;
}

double O3_recovery_factor_leaf_age(double reldev)
{
	//since MONICA does not have leaf age/classes/span
	//we define fLA as a function of development
	double crit_reldev = 0.2; //young leaves can recover fully from O3 damage
	double fLA = 1.0;
	if (reldev > crit_reldev)
	{
		fLA = std::max(0.0, 1.0 - (reldev - crit_reldev) / (1.0 - crit_reldev));
	}	
	return fLA;
}

double O3_senescence_factor(double gamma3, double O3_tot_up)
{
	//O3_tot_up �mol m - 2
	//fO3l; factor accounting for both onset and rate of senescence
	return std::max(0.5, 1.0 - gamma3 * O3_tot_up); //0.5 is arbitrary
}

double leaf_senescence_reduction_Ac(double fO3l, double reldev, double GDD_flowering, double GDD_maturity)
{
	//senescence is assumed to start at flowering in normal conditions
	double crit_reldev = GDD_flowering / GDD_maturity;
	crit_reldev *= fO3l; //correction of onset due to O3 cumulative uptake
	double fLS = 1.0;

	if (reldev > crit_reldev)
	{
		fLS = std::max(0.0, 1.0 - (reldev - crit_reldev) / (fO3l - crit_reldev)); //correction of rate due to O3 cumulative uptake
	}
	return fLS;
}

double water_stress_stomatal_closure(double upper_thr, double lower_thr, double Fshape, double FC, double WP, double SWC, double ET0)
{
	//Raes et al., 2009.  Agronomy Journal, 101(3), 438-447
	double upper_threshold_adj = upper_thr + (0.04 * (5.0 - ET0)) * std::log10(10.0 - 9.0 * upper_thr);
	if (upper_threshold_adj < 0)
	{
		upper_threshold_adj = 0;
	}
	else if (upper_threshold_adj > 1)
	{
		upper_threshold_adj = 1.0;
	}
	double WHC_adj = lower_thr - upper_threshold_adj;

	double SW_depletion_f;
	if (SWC >= FC)
	{
		SW_depletion_f = 0.0;
	}
	else if (SWC <= WP)
	{
		SW_depletion_f = 1.0;
	}
	else
	{
		SW_depletion_f = 1.0 - (SWC - WP) / (FC - WP);
	}	

	//Drel
	double Drel;
	if (SW_depletion_f <= upper_threshold_adj)
	{
		Drel = 0.0;
	}
	else if (SW_depletion_f >= lower_thr)
	{
		Drel = 1;
	}
	else
	{
		Drel = (SW_depletion_f - upper_threshold_adj) / WHC_adj;
	}
	
	return 1 - (std::exp(Drel * Fshape) - 1.0) / (std::exp(Fshape) - 1.0);
}

#pragma region 
//model composition

//"auxiliary" variables; (revise design?)
double fO3s_h[24];
double fO3s_d = 1.0;
double rO3s = 1.0;
double WS_st_clos = 1.0;
double fLA = 1.0;
double cum_O3_up = 0.0;

O3_impact_out FvCB_canopy_hourly(O3_impact_in in, O3_impact_params par)
{
	O3_impact_out out;

	if (in.h == 0)
	{
		fLA = O3_recovery_factor_leaf_age(in.reldev);
		rO3s = O3_damage_recovery(fO3s_d, fLA);
		WS_st_clos = water_stress_stomatal_closure(par.upper_thr_stomatal, par.lower_thr_stomatal, par.Fshape_stomatal, in.FC, in.WP, in.SWC, in.ET0);
	}

	double inst_O3_up = O3_uptake(in.O3a, in.gs, WS_st_clos); //nmol m-2 s-1
	cum_O3_up += inst_O3_up * 3.6; //3.6 converts from nmol to �mol and from s-1 to h-1
	out.cum_O3_up = cum_O3_up;
	fO3s_h[in.h] = hourly_O3_reduction_Ac(inst_O3_up, par.gamma1, par.gamma2);
	
	//short term O3 effect on Ac	
	fO3s_d = cumulative_O3_reduction_Ac(fO3s_h, rO3s, in.h); 
	out.fO3s_d = fO3s_d;

	//sensescence + long term O3 effect on Ac
	double fO3l = O3_senescence_factor(par.gamma3, cum_O3_up);
	out.fLS = leaf_senescence_reduction_Ac(fO3l, in.reldev, in.GDD_flo, in.GDD_mat);

	return out;
}
#pragma endregion Model composition
	
	

	
	
	
	
	