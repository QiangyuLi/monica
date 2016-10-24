/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
Authors: 
Michael Berg-Mohnicke <michael.berg@zalf.de>
Tommaso Stella <tommaso.stella@zalf.de>

Maintainers: 
Currently maintained by the authors.

This file is part of the MONICA model. 
Copyright (C) Leibniz Centre for Agricultural Landscape Research (ZALF)
*/

#include <fstream>
#include <algorithm>
#include <mutex>

#include "build-output.h"

#include "tools/json11-helper.h"
#include "tools/debug.h"
#include "tools/helper.h"
#include "tools/algorithms.h"
#include "../core/monica-model.h"

using namespace Monica;
using namespace Tools;
using namespace std;
using namespace json11;

double Monica::applyOIdOP(OId::OP op, const vector<double>& vs)
{
	double v = 0.0;
	if(!vs.empty())
	{
		v = vs.back();

		switch(op)
		{
		case OId::AVG:
			v = accumulate(vs.begin(), vs.end(), 0.0) / vs.size();
			break;
		case OId::MEDIAN:
			v = median(vs);
			break;
		case OId::SUM:
			v = accumulate(vs.begin(), vs.end(), 0.0);
			break;
		case OId::MIN:
			v = minMax(vs).first;
			break;
		case OId::MAX:
			v = minMax(vs).second;
			break;
		case OId::FIRST:
			v = vs.front();
			break;
		case OId::LAST:
		case OId::NONE:
		default:;
		}
	}

	return v;
}

Json Monica::applyOIdOP(OId::OP op, const vector<Json>& js)
{
	Json res;

	if(!js.empty() && js.front().is_array())
	{
		vector<vector<double>> dss(js.front().array_items().size());
		for(auto& j : js)
		{
			int i = 0;
			for(auto& j2 : j.array_items())
			{
				dss[i].push_back(j2.number_value());
				++i;
			}
		}

		J11Array r;
		for(auto& ds : dss)
			r.push_back(applyOIdOP(op, ds));

		res = r;
	}
	else
	{
		vector<double> ds;
		for(auto j : js)
			ds.push_back(j.number_value());
		res = applyOIdOP(op, ds);
	}

	return res;
}

//-----------------------------------------------------------------------------

vector<OId> Monica::parseOutputIds(const J11Array& oidArray)
{
	vector<OId> outputIds;

	auto getAggregationOp = [](J11Array arr, int index, OId::OP def = OId::_UNDEFINED_OP_) -> OId::OP
	{
		if(arr.size() > index && arr[index].is_string())
		{
			string ops = arr[index].string_value();
			if(toUpper(ops) == "SUM")
				return OId::SUM;
			else if(toUpper(ops) == "AVG")
				return OId::AVG;
			else if(toUpper(ops) == "MEDIAN")
				return OId::MEDIAN;
			else if(toUpper(ops) == "MIN")
				return OId::MIN;
			else if(toUpper(ops) == "MAX")
				return OId::MAX;
			else if(toUpper(ops) == "FIRST")
				return OId::FIRST;
			else if(toUpper(ops) == "LAST")
				return OId::LAST;
			else if(toUpper(ops) == "NONE")
				return OId::NONE;
		}
		return def;
	};


	auto getOrgan = [](J11Array arr, int index, OId::ORGAN def = OId::_UNDEFINED_ORGAN_) -> OId::ORGAN
	{
		if(arr.size() > index && arr[index].is_string())
		{
			string ops = arr[index].string_value();
			if(toUpper(ops) == "ROOT")
				return OId::ROOT;
			else if(toUpper(ops) == "LEAF")
				return OId::LEAF;
			else if(toUpper(ops) == "SHOOT")
				return OId::SHOOT;
			else if(toUpper(ops) == "FRUIT")
				return OId::FRUIT;
			else if(toUpper(ops) == "STRUCT")
				return OId::STRUCT;
			else if(toUpper(ops) == "SUGAR")
				return OId::SUGAR;
		}
		return def;
	};

	const auto& name2result = buildOutputTable().name2result;
	for(Json idj : oidArray)
	{
		if(idj.is_string())
		{
			string name = idj.string_value();
			auto it = name2result.find(name);
			if(it != name2result.end())
			{
				auto data = it->second;
				OId oid(data.id);
				oid.name = data.name;
				oid.unit = data.unit;
				oid.jsonInput = name;
				outputIds.push_back(oid);
			}
		}
		else if(idj.is_array())
		{
			auto arr = idj.array_items();
			if(arr.size() >= 1)
			{
				OId oid;

				string name = arr[0].string_value();
				auto it = name2result.find(name);
				if(it != name2result.end())
				{
					auto data = it->second;
					oid.id = data.id;
					oid.name = data.name;
					oid.unit = data.unit;
					oid.jsonInput = Json(arr).dump();

					if(arr.size() >= 2)
					{
						auto val1 = arr[1];
						if(val1.is_number())
						{
							oid.fromLayer = val1.int_value() - 1;
							oid.toLayer = oid.fromLayer;
						}
						else if(val1.is_string())
						{
							auto op = getAggregationOp(arr, 1);
							if(op != OId::_UNDEFINED_OP_)
								oid.timeAggOp = op;
							else
								oid.organ = getOrgan(arr, 1, OId::_UNDEFINED_ORGAN_);
						}
						else if(val1.is_array())
						{
							auto arr2 = arr[1].array_items();

							if(arr2.size() >= 1)
							{
								auto val1_0 = arr2[0];
								if(val1_0.is_number())
									oid.fromLayer = val1_0.int_value() - 1;
								else if(val1_0.is_string())
									oid.organ = getOrgan(arr2, 0, OId::_UNDEFINED_ORGAN_);
							}
							if(arr2.size() >= 2)
							{
								auto val1_1 = arr2[1];
								if(val1_1.is_number())
									oid.toLayer = val1_1.int_value() - 1;
								else if(val1_1.is_string())
								{
									oid.toLayer = oid.fromLayer;
									oid.layerAggOp = getAggregationOp(arr2, 1, OId::AVG);
								}
							}
							if(arr2.size() >= 3)
								oid.layerAggOp = getAggregationOp(arr2, 2, OId::AVG);
						}
					}
					if(arr.size() >= 3)
						oid.timeAggOp = getAggregationOp(arr, 2, OId::AVG);

					outputIds.push_back(oid);
				}
			}
		}
	}

	return outputIds;
}

//-----------------------------------------------------------------------------

template<typename T, typename Vector>
void store(OId oid, Vector& into, function<T(int)> getValue, int roundToDigits = 0)
{
	Vector multipleValues;
	vector<double> vs;
	if(oid.isOrgan())
		oid.toLayer = oid.fromLayer = int(oid.organ);

	for(int i = oid.fromLayer; i <= oid.toLayer; i++)
	{
		T v = 0;
		if(i < 0)
			debug() << "Error: " << oid.toString(true) << " has no or negative layer defined! Returning 0." << endl;
		else
			v = getValue(i);
		if(oid.layerAggOp == OId::NONE)
			multipleValues.push_back(Tools::round(v, roundToDigits));
		else
			vs.push_back(v);
	}

	if(oid.layerAggOp == OId::NONE)
		into.push_back(multipleValues);
	else
		into.push_back(applyOIdOP(oid.layerAggOp, vs));
}

BOTRes& Monica::buildOutputTable()
{
	static mutex lockable;

	//map of output ids to outputfunction
	static BOTRes m;
	static bool tableBuild = false;

	auto build = [&](Result2 r, decltype(m.ofs)::mapped_type of)
	{
		m.ofs[r.id] = of;
		m.name2result[r.name] = r;
	};

	// only initialize once
	if(!tableBuild)
	{
		lock_guard<mutex> lock(lockable);

		//test if after waiting for the lock the other thread
		//already initialized the whole thing
		if(!tableBuild)
		{
			int id = 0;

			build({id++, "Date", "", "output current date"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.currentStepDate().toIsoDateString());
			});
			
			build({id++, "Month", "", "output current Month"},
						[](const const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(int(monica.currentStepDate().month()));
			});
			
			build({id++, "Year", "", "output current Year"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(int(monica.currentStepDate().year()));
			});

			build({id++, "Crop", "", "crop name"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? monica.cropGrowth()->get_CropName() : "");
			});

			build({id++, "TraDef", "0;1", "TranspirationDeficit"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_TranspirationDeficit(), 2) : 0.0);
			});

			build({id++, "Tra", "mm", "ActualTranspiration"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_ActualTranspiration(), 2) : 0.0);
			});

			build({id++, "NDef", "0;1", "CropNRedux"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_CropNRedux(), 2) : 0.0);
			});

			build({id++, "HeatRed", "0;1", " HeatStressRedux"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_HeatStressRedux(), 2) : 0.0);
			});
			
			build({id++, "FrostRed", "0;1", "FrostStressRedux"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_FrostStressRedux(), 2) : 0.0);
			});
			
			build({id++, "OxRed", "0;1", "OxygenDeficit"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_OxygenDeficit(), 2) : 0.0);
			});

			build({id++, "Stage", "", "DevelopmentalStage"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? monica.cropGrowth()->get_DevelopmentalStage() + 1 : 0);
			});
			
			build({id++, "TempSum", "�Cd", "CurrentTemperatureSum"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_CurrentTemperatureSum(), 1) : 0.0);
			});
			
			build({id++, "VernF", "0;1", "VernalisationFactor"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_VernalisationFactor(), 2) : 0.0);
			});
			
			build({id++, "DaylF", "0;1", "DaylengthFactor"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_DaylengthFactor(), 2) : 0.0);
			});
			
			build({id++, "IncRoot", "kg ha-1", "OrganGrowthIncrement root"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_OrganGrowthIncrement(0), 2) : 0.0);
			});
			
			build({id++, "IncLeaf", "kg ha-1", "OrganGrowthIncrement leaf"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_OrganGrowthIncrement(1), 2) : 0.0);
			});
			
			build({id++, "IncShoot", "kg ha-1", "OrganGrowthIncrement shoot"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_OrganGrowthIncrement(2), 2) : 0.0);
			});
			
			build({id++, "IncFruit", "kg ha-1", "OrganGrowthIncrement fruit"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_OrganGrowthIncrement(3), 2) : 0.0);
			});
			
			build({id++, "RelDev", "0;1", "RelativeTotalDevelopment"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_RelativeTotalDevelopment(), 2) : 0.0);
			});
			
			build({id++, "LT50", "�C", "LT50"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_LT50(), 1) : 0.0);
			});
			
			build({id++, "AbBiom", "kg ha-1", "AbovegroundBiomass"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_AbovegroundBiomass(), 1) : 0.0);
			});

			build({id++, "OrgBiom", "kgDM ha-1", "get_OrganBiomass(i)"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				if(oid.isOrgan() 
					 && monica.cropGrowth()
					 && monica.cropGrowth()->get_NumberOfOrgans() >= oid.organ)
					results.push_back(round(monica.cropGrowth()->get_OrganBiomass(oid.organ), 1));
				else
					results.push_back(0.0);
			});
			
			build({id++, "Yield", "kgDM ha-1", "get_PrimaryCropYield"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_PrimaryCropYield(), 1) : 0.0);
			});

			build({id++, "SumYield", "kgDM ha-1", "get_AccumulatedPrimaryCropYield"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_AccumulatedPrimaryCropYield(), 1) : 0.0);
			});

			build({id++, "GroPhot", "kgCH2O ha-1", "GrossPhotosynthesisHaRate"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_GrossPhotosynthesisHaRate(), 4) : 0.0);
			});

			build({id++, "NetPhot", "kgCH2O ha-1", "NetPhotosynthesis"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_NetPhotosynthesis(), 2) : 0.0);
			});

			build({id++, "MaintR", "kgCH2O ha-1", "MaintenanceRespirationAS"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_MaintenanceRespirationAS(), 4) : 0.0);
			});

			build({id++, "GrowthR", "kgCH2O ha-1", "GrowthRespirationAS"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_GrowthRespirationAS(), 4) : 0.0);
			});

			build({id++, "StomRes", "s m-1", "StomataResistance"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_StomataResistance(), 2) : 0.0);
			});

			build({id++, "Height", "m", "CropHeight"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_CropHeight(), 2) : 0.0);
			});

			build({id++, "LAI", "m2 m-2", "LeafAreaIndex"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_LeafAreaIndex(), 4) : 0.0);
			});

			build({id++, "RootDep", "layer#", "RootingDepth"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? monica.cropGrowth()->get_RootingDepth() : 0);
			});

			build({id++, "EffRootDep", "m", "Effective RootingDepth"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->getEffectiveRootingDepth(), 2) : 0.0);
			});

			build({id++, "TotBiomN", "kgN ha-1", "TotalBiomassNContent"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_TotalBiomassNContent(), 1) : 0.0);
			});

			build({id++, "AbBiomN", "kgN ha-1", "AbovegroundBiomassNContent"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_AbovegroundBiomassNContent(), 1) : 0.0);
			});

			build({id++, "SumNUp", "kgN ha-1", "SumTotalNUptake"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_SumTotalNUptake(), 2) : 0.0);
			});

			build({id++, "ActNup", "kgN ha-1", "ActNUptake"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_ActNUptake(), 2) : 0.0);
			});

			build({id++, "PotNup", "kgN ha-1", "PotNUptake"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_PotNUptake(), 2) : 0.0);
			});

			build({id++, "NFixed", "kgN ha-1", "NFixed"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_BiologicalNFixation(), 2) : 0.0);
			});

			build({id++, "Target", "kgN ha-1", "TargetNConcentration"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_TargetNConcentration(), 3) : 0.0);
			});

			build({id++, "CritN", "kgN ha-1", "CriticalNConcentration"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_CriticalNConcentration(), 3) : 0.0);
			});

			build({id++, "AbBiomNc", "kgN ha-1", "AbovegroundBiomassNConcentration"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_AbovegroundBiomassNConcentration(), 3) : 0.0);
			});

			build({ id++, "Nstress", "-", "NitrogenStressIndex" }

				, [](const MonicaModel& monica, J11Array& results, OId oid)
			{
				double Nstress = 0;
				double AbBiomNc = monica.cropGrowth() ? round(monica.cropGrowth()->get_AbovegroundBiomassNConcentration(), 3) : 0.0;
				double CritN = monica.cropGrowth() ? round(monica.cropGrowth()->get_CriticalNConcentration(), 3) : 0.0;

				if (monica.cropGrowth())
				{
					Nstress = AbBiomNc < CritN ? round((AbBiomNc/ CritN), 3) : 1;
				}
				results.push_back(Nstress);
			});

			build({id++, "YieldNc", "kgN ha-1", "PrimaryYieldNConcentration"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_PrimaryYieldNConcentration(), 3) : 0.0);
			});

			build({id++, "Protein", "kg kg-1", "RawProteinConcentration"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_RawProteinConcentration(), 3) : 0.0);
			});

			build({id++, "NPP", "kgC ha-1", "NPP"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_NetPrimaryProduction(), 5) : 0.0);
			});

			build({id++, "NPP-Organs", "kgC ha-1", "organ specific NPP"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				if(oid.isOrgan()
					 && monica.cropGrowth()
					 && monica.cropGrowth()->get_NumberOfOrgans() >= oid.organ)
					results.push_back(round(monica.cropGrowth()->get_OrganSpecificNPP(oid.organ), 4));
				else
					results.push_back(0.0);
			});

			build({id++, "GPP", "kgC ha-1", "GPP"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_GrossPrimaryProduction(), 5) : 0.0);
			});

			build({id++, "Ra", "kgC ha-1", "autotrophic respiration"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(monica.cropGrowth() ? round(monica.cropGrowth()->get_AutotrophicRespiration(), 5) : 0.0);
			});

			build({id++, "Ra-Organs", "kgC ha-1", "organ specific autotrophic respiration"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				if(oid.isOrgan()
					 && monica.cropGrowth()
					 && monica.cropGrowth()->get_NumberOfOrgans() >= oid.organ)
					results.push_back(round(monica.cropGrowth()->get_OrganSpecificTotalRespired(oid.organ), 4));
				else
					results.push_back(0.0);
			});

			build({id++, "Mois", "m3 m-3", "Soil moisture content"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				store<double>(oid, results, [&](int i){ return monica.soilMoisture().get_SoilMoisture(i); }, 3);
			});

			build({id++, "Irrig", "mm", "Irrigation"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(round(monica.dailySumIrrigationWater(), 1));
			});

			build({id++, "Infilt", "mm", "Infiltration"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(round(monica.soilMoisture().get_Infiltration(), 1));
			});

			build({id++, "Surface", "mm", "Surface water storage"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(round(monica.soilMoisture().get_SurfaceWaterStorage(), 1));
			});

			build({id++, "RunOff", "mm", "Surface water runoff"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(round(monica.soilMoisture().get_SurfaceRunOff(), 1));
			});

			build({id++, "SnowD", "mm", "Snow depth"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(round(monica.soilMoisture().get_SnowDepth(), 1));
			});

			build({id++, "FrostD", "m", "Frost front depth in soil"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(round(monica.soilMoisture().get_FrostDepth(), 1));
			});

			build({id++, "ThawD", "m", "Thaw front depth in soil"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(round(monica.soilMoisture().get_ThawDepth(), 1));
			});

			build({id++, "PASW", "m3 m-3", "PASW"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				store<double>(oid, results, [&](int i)
				{ 
					return monica.soilMoisture().get_SoilMoisture(i) - monica.soilColumn().at(i).vs_PermanentWiltingPoint(); 
				}, 3);
			});

			build({id++, "SurfTemp", "�C", ""},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(round(monica.soilTemperature().get_SoilSurfaceTemperature(), 1));
			});

			build({id++, "STemp", "�C", ""},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				store<double>(oid, results, [&](int i){ return monica.soilTemperature().get_SoilTemperature(i); }, 1);
			});

			build({id++, "Act_Ev", "mm", ""},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(round(monica.soilMoisture().get_ActualEvaporation(), 1));
			});

			build({ id++, "Pot_ET", "mm", "" }

				, [](const MonicaModel& monica, J11Array& results, OId oid)
			{
				double potentialET = monica.soilMoisture().get_ET0() * monica.soilMoisture().get_KcFactor();
				results.push_back(round(potentialET, 1));
			});

			build({id++, "Act_ET", "mm", ""},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(round(monica.soilMoisture().get_Evapotranspiration(), 1));
			});

			build({id++, "ET0", "mm", ""},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(round(monica.soilMoisture().get_ET0(), 1));
			});

			build({id++, "Kc", "", ""},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(round(monica.soilMoisture().get_KcFactor(), 1));
			});

			build({id++, "AtmCO2", "ppm", "Atmospheric CO2 concentration"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(round(monica.get_AtmosphericCO2Concentration(), 0));
			});

			build({id++, "Groundw", "m", ""},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(round(monica.get_GroundwaterDepth(), 2));
			});

			build({id++, "Recharge", "mm", ""},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(round(monica.soilMoisture().get_GroundwaterRecharge(), 3));
			});

			build({id++, "NLeach", "kgN ha-1", ""},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(round(monica.soilTransport().get_NLeaching(), 3));
			});

			build({id++, "NO3", "kgN m-3", ""},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				store<double>(oid, results, [&](int i){ return monica.soilColumn().at(i).get_SoilNO3(); }, 3);
			});

			build({id++, "Carb", "kgN m-3", "Soil Carbamid"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(round(monica.soilColumn().at(0).get_SoilCarbamid(), 4));
			});

			build({id++, "NH4", "kgN m-3", ""},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				store<double>(oid, results, [&](int i){ return monica.soilColumn().at(i).get_SoilNH4(); }, 4);
			});

			build({id++, "NO2", "kgN m-3", ""},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				store<double>(oid, results, [&](int i){ return monica.soilColumn().at(i).get_SoilNO2(); }, 4);
			});

			build({id++, "SOC", "kgC kg-1", "get_SoilOrganicC"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				store<double>(oid, results, [&](int i){ return monica.soilColumn().at(i).vs_SoilOrganicCarbon(); }, 4);
			});

			build({id++, "SOC-X-Y", "gC m-2", "SOC-X-Y"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				store<double>(oid, results, [&](int i)
				{
					return monica.soilColumn().at(i).vs_SoilOrganicCarbon()
						* monica.soilColumn().at(i).vs_SoilBulkDensity()
						* monica.soilColumn().at(i).vs_LayerThickness
						* 1000;
				}, 4);
			});

			build({id++, "AOMf", "kgC m-3", "get_AOM_FastSum"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				store<double>(oid, results, [&](int i){ return monica.soilOrganic().get_AOM_FastSum(i); }, 4);
			});

			build({id++, "AOMs", "kgC m-3", "get_AOM_SlowSum"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				store<double>(oid, results, [&](int i){ return monica.soilOrganic().get_AOM_SlowSum(i); }, 4);
			});

			build({id++, "SMBf", "kgC m-3", "get_SMB_Fast"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				store<double>(oid, results, [&](int i){ return monica.soilOrganic().get_SMB_Fast(i); }, 4);
			});

			build({id++, "SMBs", "kgC m-3", "get_SMB_Slow"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				store<double>(oid, results, [&](int i){ return monica.soilOrganic().get_SMB_Slow(i); }, 4);
			});

			build({id++, "SOMf", "kgC m-3", "get_SOM_Fast"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				store<double>(oid, results, [&](int i){ return monica.soilOrganic().get_SOM_Fast(i); }, 4);
			});

			build({id++, "SOMs", "kgC m-3", "get_SOM_Slow"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				store<double>(oid, results, [&](int i){ return monica.soilOrganic().get_SOM_Slow(i); }, 4);
			});

			build({id++, "CBal", "kgC m-3", "get_CBalance"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				store<double>(oid, results, [&](int i){ return monica.soilOrganic().get_CBalance(i); }, 4);
			});

			build({id++, "Nmin", "kgN ha-1", "NetNMineralisationRate"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				store<double>(oid, results, [&](int i){ return monica.soilOrganic().get_NetNMineralisationRate(i); }, 6);
			});

			build({id++, "NetNmin", "kgN ha-1", "NetNmin"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(round(monica.soilOrganic().get_NetNMineralisation(), 5));
			});

			build({id++, "Denit", "kgN ha-1", "Denit"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(round(monica.soilOrganic().get_Denitrification(), 5));
			});

			build({id++, "N2O", "kgN ha-1", "N2O"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(round(monica.soilOrganic().get_N2O_Produced(), 5));
			});

			build({id++, "SoilpH", "", "SoilpH"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(round(monica.soilColumn().at(0).get_SoilpH(), 1));
			});

			build({id++, "NEP", "kgC ha-1", "NEP"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(round(monica.soilOrganic().get_NetEcosystemProduction(), 5));
			});

			build({id++, "NEE", "kgC ha-", "NEE"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(round(monica.soilOrganic().get_NetEcosystemExchange(), 5));
			});

			build({id++, "Rh", "kgC ha-", "Rh"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(round(monica.soilOrganic().get_DecomposerRespiration(), 5));
			});

			build({id++, "Tmin", "", ""},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				const auto& cd = monica.currentStepClimateData();
				auto ci = cd.find(Climate::tmin);
				results.push_back(ci == cd.end() ? 0.0 : round(ci->second, 4));
			});

			build({id++, "Tavg", "", ""},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				const auto& cd = monica.currentStepClimateData();
				auto ci = cd.find(Climate::tavg);
				results.push_back(ci == cd.end() ? 0.0 : round(ci->second, 4));
			});

			build({id++, "Tmax", "", ""}, 
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				const auto& cd = monica.currentStepClimateData();
				auto ci = cd.find(Climate::tmax);
				results.push_back(ci == cd.end() ? 0.0 : round(ci->second, 4));
			});

			build({id++, "Precip", "mm", "Precipitation"}, 
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				const auto& cd = monica.currentStepClimateData();
				auto ci = cd.find(Climate::precip);
				results.push_back(ci == cd.end() ? 0.0 : round(ci->second, 4));
			});

			build({id++, "Wind", "", ""},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				const auto& cd = monica.currentStepClimateData();
				auto ci = cd.find(Climate::wind);
				results.push_back(ci == cd.end() ? 0.0 : round(ci->second, 4));
			});

			build({id++, "Globrad", "", ""},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				const auto& cd = monica.currentStepClimateData();
				auto ci = cd.find(Climate::globrad);
				results.push_back(ci == cd.end() ? 0.0 : round(ci->second, 4));
			});

			build({id++, "Relhumid", "", ""},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				const auto& cd = monica.currentStepClimateData();
				auto ci = cd.find(Climate::relhumid);
				results.push_back(ci == cd.end() ? 0.0 : round(ci->second, 4));
			});

			build({id++, "Sunhours", "", ""},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				const auto& cd = monica.currentStepClimateData();
				auto ci = cd.find(Climate::sunhours);
				results.push_back(ci == cd.end() ? 0.0 : round(ci->second, 4));
			});

			build({id++, "BedGrad", "0;1", ""},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(round(monica.soilMoisture().get_PercentageSoilCoverage(), 3));
			});

			build({id++, "N", "kgN m-3", ""},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				store<double>(oid, results, [&](int i){ return monica.soilColumn().at(i).get_SoilNmin(); }, 3);
			});

			build({id++, "Co", "kgC m-3", ""},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				store<double>(oid, results, [&](int i){ return monica.soilOrganic().get_SoilOrganicC(i); }, 2);
			});

			build({id++, "NH3", "kgN ha-1", "NH3_Volatilised"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(round(monica.soilOrganic().get_NH3_Volatilised(), 3));
			});

			build({id++, "NFert", "kgN ha-1", "dailySumFertiliser"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(round(monica.dailySumFertiliser(), 1));
			});

			build({id++, "WaterContent", "%nFC", "soil water content"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				store<double>(oid, results, [&](int i)
				{
					double smm3 = monica.soilMoisture().get_SoilMoisture(i); // soilc.at(i).get_Vs_SoilMoisture_m3();
					double fc = monica.soilColumn().at(i).vs_FieldCapacity();
					double pwp = monica.soilColumn().at(i).vs_PermanentWiltingPoint();
					return smm3 / (fc - pwp); //[%nFK]
				}, 4);
			});

			build({id++, "CapillaryRise", "mm", "capillary rise"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				store<double>(oid, results, [&](int i){ return monica.soilMoisture().get_CapillaryRise(i); }, 3);
			});

			build({id++, "PercolationRate", "mm", "percolation rate"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				store<double>(oid, results, [&](int i){ return monica.soilMoisture().get_PercolationRate(i); }, 3);
			});

			build({id++, "SMB-CO2-ER", "", "soilOrganic.get_SMB_CO2EvolutionRate"},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				store<double>(oid, results, [&](int i){ return monica.soilOrganic().get_SMB_CO2EvolutionRate(i); }, 1);
			});

			build({id++, "Evapotranspiration", "mm", ""},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(round(monica.getEvapotranspiration(), 1));
			});

			build({id++, "Evaporation", "mm", ""},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(round(monica.getEvaporation(), 1));
			});

			build({id++, "Transpiration", "mm", ""},
						[](const MonicaModel& monica, J11Array& results, OId oid)
			{
				results.push_back(round(monica.getTranspiration(), 1));
			});

			tableBuild = true;
		}
	}

	return m;
}

//-----------------------------------------------------------------------------
