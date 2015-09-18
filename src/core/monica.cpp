/**
Authors: 
Dr. Claas Nendel <claas.nendel@zalf.de>
Xenia Specka <xenia.specka@zalf.de>
Michael Berg <michael.berg@zalf.de>

Maintainers: 
Currently maintained by the authors.

This file is part of the MONICA model. 
Copyright (C) 2007-2013, Leibniz Centre for Agricultural Landscape Research (ZALF)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <set>
#include <sstream>
#include <mutex>
#include <memory>
#include <chrono>
#include <thread>

#include "tools/debug.h"
#include "monica.h"
#include "climate/climate-common.h"
#include "db/abstract-db-connections.h"

using namespace Monica;
using namespace std;
using namespace Climate;
using namespace Tools;
using namespace Soil;

namespace
{
	//! simple functor for use in fertiliser trigger
  struct AddFertiliserAmountsCallback
  {
		double& _sf;
		double& _dsf;
		AddFertiliserAmountsCallback(double& sf, double& dsf) : _sf(sf), _dsf(dsf) { }
    void operator()(double v)
    {
            // cout << "!!!AddFertiliserAmountsCallback: daily_sum_fert += " << v << endl;
			_sf += v;
			_dsf += v;
		}
	};

}

//------------------------------------------------------------------------------

/**
 * @brief Simulation of crop seed.
 * @param crop to be planted
 */
void MonicaModel::seedCrop(CropPtr crop)
{
  debug() << "seedCrop" << endl;
  delete _currentCropGrowth;
  p_daysWithCrop = 0;
  p_accuNStress = 0.0;
  p_accuWaterStress = 0.0;
  p_accuHeatStress = 0.0;
  p_accuOxygenStress = 0.0;

  _currentCropGrowth = NULL;
  _currentCrop = crop;
  if(_currentCrop->isValid())
  {
    const CropParameters* cps = _currentCrop->cropParameters();
    _currentCropGrowth = new CropGrowth(_soilColumn, _generalParams,
                                        *cps, _siteParams, _centralParameterProvider, crop->getEva2TypeUsage());

    if (_currentCrop->perennialCropParameters())
      _currentCropGrowth->setPerennialCropParameters(_currentCrop->perennialCropParameters());

    _soilTransport.put_Crop(_currentCropGrowth);
    _soilColumn.put_Crop(_currentCropGrowth);
    _soilMoisture.put_Crop(_currentCropGrowth);
    _soilOrganic.put_Crop(_currentCropGrowth);

    debug() << "seedDate: "<< _currentCrop->seedDate().toString()
            << " harvestDate: " << _currentCrop->harvestDate().toString() << endl;

    if(_generalParams.useNMinMineralFertilisingMethod &&
       _currentCrop->seedDate().dayOfYear() <= _currentCrop->harvestDate().dayOfYear())
    {
      debug() << "nMin fertilising summer crop" << endl;
      double fert_amount = applyMineralFertiliserViaNMinMethod
                           (_generalParams.nMinFertiliserPartition,
                            NMinCropParameters(cps->pc_SamplingDepth,
                                               cps->pc_TargetNSamplingDepth,
                                               cps->pc_TargetN30));
      addDailySumFertiliser(fert_amount);
    }

    if(this->writeOutputFiles())
      _currentCrop->writeCropParameters(_generalParams.pathToOutputDir);
  }
}

/**
 * @brief Simulating harvest of crop.
 *
 * Deletes the current crop.
 */
void MonicaModel::harvestCurrentCrop(bool exported)
{
	//could be just a fallow, so there might be no CropGrowth object
	if (_currentCrop.get() && _currentCrop->isValid())
  {
		if (!exported){
			//prepare to add the total plant to soilorganic (AOMs)
			double abovegroundBiomass = _currentCropGrowth->get_AbovegroundBiomass();
			double abovegroundBiomassNConcentration = 
				_currentCropGrowth->get_AbovegroundBiomassNConcentration();
			debug() << "adding organic matter from aboveground biomass to soilOrganic" << endl;
			debug() << "aboveground biomass: " << abovegroundBiomass
				<< " Aboveground biomass N concentration: " << abovegroundBiomassNConcentration << endl;
			double rootBiomass = _currentCropGrowth->get_OrganBiomass(0);
			double rootNConcentration = _currentCropGrowth->get_RootNConcentration();
			debug() << "adding organic matter from root to soilOrganic" << endl;
			debug() << "root biomass: " << rootBiomass
				<< " Root N concentration: " << rootNConcentration << endl;
			
			_soilOrganic.addOrganicMatter(_currentCrop->residueParameters(),
				abovegroundBiomass, abovegroundBiomassNConcentration);
			_soilOrganic.addOrganicMatter(_currentCrop->residueParameters(),
				rootBiomass, rootNConcentration);
		}
		else 
		{
			//prepare to add root and crop residues to soilorganic (AOMs)
			double rootBiomass = _currentCropGrowth->get_OrganBiomass(0);
			double rootNConcentration = _currentCropGrowth->get_RootNConcentration();
			debug() << "adding organic matter from root to soilOrganic" << endl;
			debug() << "root biomass: " << rootBiomass
				<< " Root N concentration: " << rootNConcentration << endl;

			_soilOrganic.addOrganicMatter(_currentCrop->residueParameters(),
				rootBiomass, rootNConcentration); 
			
			double residueBiomass =
        _currentCropGrowth->get_ResidueBiomass(_generalParams.useSecondaryYields);
			//!@todo Claas: das hier noch berechnen
			double residueNConcentration = _currentCropGrowth->get_ResiduesNConcentration();
			debug() << "adding organic matter from residues to soilOrganic" << endl;
			debug() << "residue biomass: " << residueBiomass
				<< " Residue N concentration: " << residueNConcentration << endl;
			debug() << "primary yield biomass: " << _currentCropGrowth->get_PrimaryCropYield()
				<< " Primary yield N concentration: " << _currentCropGrowth->get_PrimaryYieldNConcentration() << endl;
			debug() << "secondary yield biomass: " << _currentCropGrowth->get_SecondaryCropYield()
				<< " Secondary yield N concentration: " << _currentCropGrowth->get_PrimaryYieldNConcentration() << endl;
			debug() << "Residues N content: " << _currentCropGrowth->get_ResiduesNContent()
				<< " Primary yield N content: " << _currentCropGrowth->get_PrimaryYieldNContent()
				<< " Secondary yield N content: " << _currentCropGrowth->get_SecondaryYieldNContent() << endl;

			_soilOrganic.addOrganicMatter(_currentCrop->residueParameters(),
				residueBiomass, residueNConcentration);
		}
	}

  delete _currentCropGrowth;
  _currentCropGrowth = NULL;
  _currentCrop = CropPtr();
  _soilTransport.remove_Crop();
  _soilColumn.remove_Crop();
  _soilMoisture.remove_Crop();
  _soilOrganic.remove_Crop();
}

void MonicaModel::fruitHarvestCurrentCrop(double percentage, bool exported)
{
	//could be just a fallow, so there might be no CropGrowth object
	if (_currentCrop.get() && _currentCrop->isValid())
	{
		//prepare to remove fruit
		double totalBiomassNContent = _currentCropGrowth->get_TotalBiomassNContent();
		double currentFruitBiomass = _currentCropGrowth->get_OrganBiomass(3);
		double currentFruitNContent = _currentCropGrowth->get_FruitBiomassNContent();
//		double fruitToRemove = percentage * currentFruitBiomass;
		double fruitNToRemove = percentage * currentFruitNContent;
		double fruitToRemain = (1.0 - percentage) * currentFruitBiomass;
		double totalBiomassNToRemain = totalBiomassNContent - fruitNToRemove;
		
		_currentCropGrowth->accumulatePrimaryCropYield(_currentCropGrowth->get_PrimaryCropYield());
		_currentCropGrowth->set_OrganBiomass(3, fruitToRemain);
		_currentCropGrowth->set_TotalBiomassNContent(totalBiomassNToRemain);

		if (!exported){
		}
		else 
		{
			//no crop residues are added to soilorganic (AOMs)
			debug() << "adding no organic matter from fruit residues to soilOrganic" << endl;
		}
	}
}


void MonicaModel::leafPruningCurrentCrop(double percentage, bool exported)
{
	//could be just a fallow, so there might be no CropGrowth object
	if (_currentCrop.get() && _currentCrop->isValid())
	{
		//prepare to remove leaves
		double currentLeafBiomass = _currentCropGrowth->get_OrganBiomass(1);
		double leavesToRemove = percentage * currentLeafBiomass;
		double leavesToRemain = (1.0 - percentage) * currentLeafBiomass;
		_currentCropGrowth->set_OrganBiomass(1, leavesToRemain);

		if (!exported){
			//prepare to add crop residues to soilorganic (AOMs)
			double leafResidueNConcentration = _currentCropGrowth->get_ResiduesNConcentration();
			debug() << "adding organic matter from leaf residues to soilOrganic" << endl;
			debug() << "leaf residue biomass: " << leavesToRemove
				<< " Leaf residue N concentration: " << leafResidueNConcentration << endl;
			
			_soilOrganic.addOrganicMatter(_currentCrop->residueParameters(),
				leavesToRemove, leafResidueNConcentration);

		}
		
	}
}

void MonicaModel::tipPruningCurrentCrop(double percentage, bool exported)
{
	//could be just a fallow, so there might be no CropGrowth object
	if (_currentCrop.get() && _currentCrop->isValid())
	{
		//prepare to remove tips
		double currentLeafBiomass = _currentCropGrowth->get_OrganBiomass(1);
		double currentShootBiomass = _currentCropGrowth->get_OrganBiomass(2);
		double leavesToRemove = percentage * currentLeafBiomass;
		double shootsToRemove = percentage * currentShootBiomass;
		double leavesToRemain = (1.0 - percentage) * currentLeafBiomass;
		double shootsToRemain = (1.0 - percentage) * currentShootBiomass;
		_currentCropGrowth->set_OrganBiomass(1, leavesToRemain);
		_currentCropGrowth->set_OrganBiomass(2, shootsToRemain);

		if (!exported){
			//prepare to add crop residues to soilorganic (AOMs)
			double tipResidues = leavesToRemove + shootsToRemove;
			double tipResidueNConcentration = _currentCropGrowth->get_ResiduesNConcentration();
			debug() << "adding organic matter from tip residues to soilOrganic" << endl;
			debug() << "Tip residue biomass: " << tipResidues
				<< " Tip residue N concentration: " << tipResidueNConcentration << endl;

			_soilOrganic.addOrganicMatter(_currentCrop->residueParameters(),
				tipResidues, tipResidueNConcentration);
		}
	}
}

	void MonicaModel::shootPruningCurrentCrop(double percentage, bool exported)
{
	//could be just a fallow, so there might be no CropGrowth object
	if (_currentCrop.get() && _currentCrop->isValid())
	{
		//prepare to remove tips
		double currentLeafBiomass = _currentCropGrowth->get_OrganBiomass(1);
		double currentShootBiomass = _currentCropGrowth->get_OrganBiomass(2);
		double leavesToRemove = percentage * currentLeafBiomass;
		double shootsToRemove = percentage * currentShootBiomass;
		double leavesToRemain = (1.0 - percentage) * currentLeafBiomass;
		double shootsToRemain = (1.0 - percentage) * currentShootBiomass;
		_currentCropGrowth->set_OrganBiomass(1, leavesToRemain);
		_currentCropGrowth->set_OrganBiomass(2, shootsToRemain);

		if (!exported){
			//prepare to add crop residues to soilorganic (AOMs)
			double tipResidues = leavesToRemove + shootsToRemove;
			double tipResidueNConcentration = _currentCropGrowth->get_ResiduesNConcentration();
			debug() << "adding organic matter from shoot and leaf residues to soilOrganic" << endl;
			debug() << "Shoot and leaf residue biomass: " << tipResidues
				<< " Tip residue N concentration: " << tipResidueNConcentration << endl;

			_soilOrganic.addOrganicMatter(_currentCrop->residueParameters(),
				tipResidues, tipResidueNConcentration);
		}
	}
}
/**
 * @brief Simulating plowing or incorporating of total crop.
 *
 * Deletes the current crop.
 */
void MonicaModel::incorporateCurrentCrop()
{
  //could be just a fallow, so there might be no CropGrowth object
  if(_currentCrop && _currentCrop->isValid())
  {
    //prepare to add root and crop residues to soilorganic (AOMs)
    double total_biomass = _currentCropGrowth->totalBiomass();
    double totalNConcentration = _currentCropGrowth->get_AbovegroundBiomassNConcentration() + _currentCropGrowth->get_RootNConcentration();

    debug() << "Adding organic matter from total biomass of crop to soilOrganic" << endl;
    debug() << "Total biomass: " << total_biomass << endl
        << " Total N concentration: " << totalNConcentration << endl;

    _soilOrganic.addOrganicMatter(_currentCrop->residueParameters(),
                                  total_biomass, totalNConcentration);
  }

  delete _currentCropGrowth;
  _currentCropGrowth = NULL;
  _currentCrop = CropPtr();
  _soilTransport.remove_Crop();
  _soilColumn.remove_Crop();
  _soilMoisture.remove_Crop();
}



/**
 * @brief Simulating cutting of crop.
 *
 * Only removes some biomass of crop but not harvesting the crop.
 */

void MonicaModel::cuttingCurrentCrop(double percentage, bool exported)
{
	//could be just a fallow, so there might be no CropGrowth object
	if (_currentCrop.get() && _currentCrop->isValid())
	{
		//prepare to remove tips
		double currentLeafBiomass = _currentCropGrowth->get_OrganBiomass(1);
		double currentShootBiomass = _currentCropGrowth->get_OrganBiomass(2);
		double currentFruitBiomass = _currentCropGrowth->get_OrganBiomass(3);
		double leavesToRemove = percentage * currentLeafBiomass;
		double shootsToRemove = percentage * currentShootBiomass;
		double fruitsToRemove = currentFruitBiomass;
		double leavesToRemain = (1.0 - percentage) * currentLeafBiomass;
		double shootsToRemain = (1.0 - percentage) * currentShootBiomass;
		int stageAfterCut = _currentCropGrowth->get_StageAfterCut();
		_currentCropGrowth->accumulatePrimaryCropYield(_currentCropGrowth->get_CropYieldAfterCutting());
		_currentCropGrowth->set_OrganBiomass(1, leavesToRemain);
		_currentCropGrowth->set_OrganBiomass(2, shootsToRemain);
		_currentCropGrowth->set_OrganBiomass(3, 0.0); // fruit is not present after cutting
		_currentCropGrowth->set_OrganBiomass(5, 0.0); // sugar is not present after cutting
		_currentCropGrowth->set_DevelopmentalStage(stageAfterCut); // sets developmentag stage according to crop database
		_currentCropGrowth->set_CuttingDelayDays(); // sets delay after cutting according to crop database
		_currentCropGrowth->set_MaxAssimilationRate(0.9); // Reduces maximum assimilation rate by 10%

		if (!exported){
			//prepare to add crop residues to soilorganic (AOMs)
			double residues = leavesToRemove + shootsToRemove + fruitsToRemove;
			double residueNConcentration = _currentCropGrowth->get_AbovegroundBiomassNConcentration();
			debug() << "adding organic matter from cut residues to soilOrganic" << endl;
			debug() << "Residue biomass: " << residues
				<< " Residue N concentration: " << residueNConcentration << endl;

			_soilOrganic.addOrganicMatter(_currentCrop->residueParameters(),
				residues, residueNConcentration);
		}
	}
}


/**
 * @brief Applying of fertilizer.
 *
 * @todo Nothing implemented yet.
 */
void MonicaModel::applyMineralFertiliser(MineralFertiliserParameters partition,
                                         double amount)
{
  if(!_generalParams.useNMinMineralFertilisingMethod)
  {
		_soilColumn.applyMineralFertiliser(partition, amount);
		addDailySumFertiliser(amount);
	}
}

void MonicaModel::applyOrganicFertiliser(const OrganicMatterParameters* params,
																				 double amount, bool incorporation)
{
  debug() << "MONICA model: applyOrganicFertiliser:\t" << amount << "\t" << params->vo_NConcentration << endl;
  _soilOrganic.setIncorporation(incorporation);
  _soilOrganic.addOrganicMatter(params, amount, params->vo_NConcentration);
  addDailySumFertiliser(amount * params->vo_NConcentration);
}

double MonicaModel::
    applyMineralFertiliserViaNMinMethod(MineralFertiliserParameters partition,
        NMinCropParameters cps)
{
  //AddFertiliserAmountsCallback x(_sumFertiliser, _dailySumFertiliser);

  const NMinUserParameters& ups = _generalParams.nMinUserParams;

  double fert_amount = _soilColumn.applyMineralFertiliserViaNMinMethod
      (partition, cps.samplingDepth, cps.nTarget, cps.nTarget30,
       ups.min, ups.max, ups.delayInDays);
  return fert_amount;

  //ref(_sumFertiliser) += _1);
}

void MonicaModel::applyIrrigation(double amount, double nitrateConcentration,
                                  double /*sulfateConcentration*/)
{
  //if the production process has still some defined manual irrigation dates
  if(!_generalParams.useAutomaticIrrigation)
  {
    _soilOrganic.addIrrigationWater(amount);
    _soilColumn.applyIrrigation(amount, nitrateConcentration);
    if(_currentCrop)
    {
      _currentCrop->addAppliedIrrigationWater(amount);
      addDailySumIrrigationWater(amount);
    }
  }
}

/**
 * Applies tillage for a given soil depth. Tillage means in MONICA,
 * that for all effected soil layer the parameters are averaged.
 * @param depth Depth in meters
 */
void MonicaModel::applyTillage(double depth)
{
	_soilColumn.applyTillage(depth);
}

/**
 * @brief Simulating the soil processes for one time step.
 * @param stepNo Number of current processed step
 */
void MonicaModel::generalStep(Date date, std::map<ACD, double> climateData)
{
  unsigned int julday = date.julianDay();
//  unsigned int year = currentDate.year();
  bool leapYear = date.isLeapYear();
  double tmin = climateData[Climate::tmin];
  double tavg = climateData[Climate::tavg];
  double tmax = climateData[Climate::tmax];
  double precip = climateData[Climate::precip];
  double wind = climateData[Climate::wind];
  double globrad = climateData[Climate::globrad];

  // test if data for relhumid are available; if not, value is set to -1.0
  double relhumid = climateData.find(Climate::relhumid) == climateData.end()
                    ? -1.0
                    : climateData[Climate::relhumid];

//  cout << "tmin:\t" << tmin << endl;
//  cout << "tavg:\t" << tavg << endl;
//  cout << "tmax:\t" << tmax << endl;
//  cout << "precip:\t" << precip << endl;
//  cout << "wind:\t" << wind << endl;
//  cout << "globrad:\t" << globrad << endl;
//  cout << "relhumid:\t" << relhumid << endl;
  // write climate data into separate file with hermes format
//  std::ofstream climate_file((_env.pathToOutputDir+"climate.csv").c_str(), ios_base::app);
//  if (stepNo==0) {
//      climate_file << "Tp_av\t"<<"Tpmin\t"<<"Tpmax\t"<<"T_s10\t"<<"T_s20\t"<<"vappd\t"<<"wind\t"<<"sundu\t"<<"radia\t"<<"prec\t"<<"tagesnummer\t"<<"RF\t"<< endl;
//      climate_file << "C_deg\t"<<"C_deg\t"<<"C_deg\t"<<"C_deg\t"<<"C_deg\t"<<"mm_Hg\t"<<"m/se\t"<<"hours\t"<<"J/cm²\t"<<"mm\t"<<"jday\t"<<"%\t"<<endl;
//      climate_file << "-----------------------------------" << endl;
// }
//
//  climate_file << tavg <<"\t" << tmin <<"\t" << tmax <<"\t" << 0.0 <<"\t" << 0.0 <<"\t"
//      << 0.0 <<"\t" << wind <<"\t" << sunhours <<"\t" << globrad <<"\t" << precip <<"\t" << stepNo <<"\t" << relhumid << endl;
//  climate_file.close();


  const UserEnvironmentParameters& user_env = _centralParameterProvider.userEnvironmentParameters;
  vw_AtmosphericCO2Concentration = _generalParams.atmosphericCO2 == -1
                                   ? user_env.p_AthmosphericCO2
                                   : _generalParams.atmosphericCO2;


  // test if simulated gw or measured values should be used
  double gw_value = getGroundwaterInformation(date);

  if (gw_value == -1) {
//  cout << "vs_GroundwaterDepth:\t" << user_env.p_MinGroundwaterDepth << "\t" << user_env.p_MaxGroundwaterDepth << endl;
  vs_GroundwaterDepth = GroundwaterDepthForDate(user_env.p_MaxGroundwaterDepth,
				        user_env.p_MinGroundwaterDepth,
				        user_env.p_MinGroundwaterDepthMonth,
				        julday,
				        leapYear);
  } else {
      vs_GroundwaterDepth = gw_value / 100.0; // [cm] --> [m]
  }

  if (int(vw_AtmosphericCO2Concentration) == 0)
    vw_AtmosphericCO2Concentration = CO2ForDate(date);

  //  debug << "step: " << stepNo << " p: " << precip << " gr: " << globrad << endl;

  //31 + 28 + 15
  unsigned int pc_JulianDayAutomaticFertilising =
      user_env.p_JulianDayAutomaticFertilising;

  _soilColumn.deleteAOMPool();

  _soilColumn.applyPossibleDelayedFerilizer();
  double delayed_fert_amount = _soilColumn.applyPossibleTopDressing();
  addDailySumFertiliser(delayed_fert_amount);

  if(_currentCrop &&
     _currentCrop->isValid() &&
     _generalParams.useNMinMineralFertilisingMethod &&
     _currentCrop->seedDate().dayOfYear() > _currentCrop->harvestDate().dayOfYear() &&
     julday == pc_JulianDayAutomaticFertilising)
  {
    debug() << "nMin fertilising winter crop" << endl;
		const CropParameters* cps = _currentCrop->cropParameters();
    double fert_amount = applyMineralFertiliserViaNMinMethod
                         (_generalParams.nMinFertiliserPartition,
                          NMinCropParameters(cps->pc_SamplingDepth,
                                             cps->pc_TargetNSamplingDepth,
                                             cps->pc_TargetN30));
    addDailySumFertiliser(fert_amount);
	}

  _soilTemperature.step(tmin, tmax, globrad);
  _soilMoisture.step(vs_GroundwaterDepth,
                     precip, tmax, tmin,
                     (relhumid / 100.0), tavg, wind,
                     _generalParams.windSpeedHeight,
		 globrad, julday);

  _soilOrganic.step(tavg, precip, wind);
  _soilTransport.step();
}

void MonicaModel::generalStep(unsigned int stepNo)
{
  Date startDate = _dataAccessor.startDate();
  Date currentDate = startDate + stepNo;
  double tmin = _dataAccessor.dataForTimestep(Climate::tmin, stepNo);
  double tavg = _dataAccessor.dataForTimestep(Climate::tavg, stepNo);
  double tmax = _dataAccessor.dataForTimestep(Climate::tmax, stepNo);
  double precip = _dataAccessor.dataForTimestep(Climate::precip, stepNo);
  double wind = _dataAccessor.dataForTimestep(Climate::wind, stepNo);
  double globrad = _dataAccessor.dataForTimestep(Climate::globrad, stepNo);

  // test if data for relhumid are available; if not, value is set to -1.0
  double relhumid = _dataAccessor.hasAvailableClimateData(Climate::relhumid) ?
       _dataAccessor.dataForTimestep(Climate::relhumid, stepNo) : -1.0;

  generalStep(currentDate, {{Climate::tmin, tmin},
                            {Climate::tavg, tavg},
                            {Climate::tmax, tmax},
                            {Climate::precip, precip},
                            {Climate::wind, wind},
                            {Climate::globrad, globrad},
                            {Climate::relhumid, relhumid}});
}

void MonicaModel::cropStep(Tools::Date date, std::map<Climate::ACD, double> climateData)
{
  // do nothing if there is no crop
  if(!_currentCropGrowth)
    return;

  p_daysWithCrop++;

  unsigned int julday = date.julianDay();

  double tavg = climateData[Climate::tavg];
  double tmax = climateData[Climate::tmax];
  double tmin = climateData[Climate::tmin];
  double globrad = climateData[Climate::globrad];

  // test if data for sunhours are available; if not, value is set to -1.0
  double sunhours = climateData.find(Climate::sunhours) == climateData.end()
                    ? -1.0 : climateData[Climate::sunhours];

  // test if data for relhumid are available; if not, value is set to -1.0
  double relhumid = climateData.find(Climate::relhumid) == climateData.end()
                    ? -1.0 : climateData[Climate::relhumid];

  double wind =  climateData[Climate::wind];
  double precip =  climateData[Climate::precip];

  double vw_WindSpeedHeight = _centralParameterProvider.userEnvironmentParameters.p_WindSpeedHeight;

  _currentCropGrowth->step(tavg, tmax, tmin, globrad, sunhours, julday,
                           (relhumid / 100.0), wind, vw_WindSpeedHeight,
                           vw_AtmosphericCO2Concentration, precip);
  if(_generalParams.useAutomaticIrrigation)
  {
    const AutomaticIrrigationParameters& aips = _generalParams.autoIrrigationParams;
    if(_soilColumn.applyIrrigationViaTrigger(aips.treshold, aips.amount,
                                             aips.nitrateConcentration))
    {
      _soilOrganic.addIrrigationWater(aips.amount);
      _currentCrop->addAppliedIrrigationWater(aips.amount);
      _dailySumIrrigationWater += aips.amount;
    }
  }

  p_accuNStress += _currentCropGrowth->get_CropNRedux();
  p_accuWaterStress += _currentCropGrowth->get_TranspirationDeficit();
  p_accuHeatStress += _currentCropGrowth->get_HeatStressRedux();
  p_accuOxygenStress += _currentCropGrowth->get_OxygenDeficit();
}


/**
 * @brief Simulating crop growth for one time step.
 */
void MonicaModel::cropStep(unsigned int stepNo)
{
  Date startDate = _dataAccessor.startDate();
  Date currentDate = startDate + stepNo;
  double tavg = _dataAccessor.dataForTimestep(Climate::tavg, stepNo);
  double tmax = _dataAccessor.dataForTimestep(Climate::tmax, stepNo);
  double tmin = _dataAccessor.dataForTimestep(Climate::tmin, stepNo);
  double globrad = _dataAccessor.dataForTimestep(Climate::globrad, stepNo);

  // test if data for sunhours are available; if not, value is set to -1.0
  double sunhours = _dataAccessor.hasAvailableClimateData(Climate::sunhours) ?
	  _dataAccessor.dataForTimestep(Climate::sunhours, stepNo) : -1.0;		

  // test if data for relhumid are available; if not, value is set to -1.0
  double relhumid = _dataAccessor.hasAvailableClimateData(Climate::relhumid) ?
      _dataAccessor.dataForTimestep(Climate::relhumid, stepNo) : -1.0;

  double wind =  _dataAccessor.dataForTimestep(Climate::wind, stepNo);
  double precip =  _dataAccessor.dataForTimestep(Climate::precip, stepNo);

  cropStep(currentDate, {{Climate::tmin, tmin},
                         {Climate::tavg, tavg},
                         {Climate::tmax, tmax},
                         {Climate::precip, precip},
                         {Climate::wind, wind},
                         {Climate::globrad, globrad},
                         {Climate::relhumid, relhumid},
                         {Climate::sunhours, sunhours}});
}

/**
* @brief Returns atmospheric CO2 concentration for date [ppm]
*
* @param year, julianDay, leapYear
*
* @return
*/
double MonicaModel::CO2ForDate(double year, double julianDay, bool leapYear)
{
  double decimalDate = year + julianDay/(leapYear ? 366.0 : 365);

  //Atmospheric CO2 concentration according to RCP 8.5
  return 222.0 + exp(0.01467*(decimalDate - 1650.0)) + 2.5*sin((decimalDate - 0.5)/0.1592);
}

double MonicaModel::CO2ForDate(Date d)
{
  double decimalDate = d.year() + d.julianDay()/(d.useLeapYears() && d.isLeapYear() ? 366.0 : 365);

  //Atmospheric CO2 concentration according to RCP 8.5
  return 222.0 + exp(0.01467*(decimalDate - 1650.0)) + 2.5*sin((decimalDate - 0.5)/0.1592);
}


/**
* @brief Returns groundwater table for date [m]
*
* @param pm_MaxGroundwaterTable; pm_MinGroundwaterTable; pm_MaxGroundwaterTableMonth
*
* @return
*/
double MonicaModel::GroundwaterDepthForDate(double maxGroundwaterDepth,
                                            double minGroundwaterDepth,
                                            int minGroundwaterDepthMonth,
                                            double julianday,
                                            bool leapYear)
{
  double days = 365;
  if(leapYear)
    days = 366.0;

  double meanGroundwaterDepth = (maxGroundwaterDepth + minGroundwaterDepth) / 2.0;
  double groundwaterAmplitude = (maxGroundwaterDepth - minGroundwaterDepth) / 2.0;

  double sinus = sin(((julianday/days*360.0) - 90.0 -
                      ((double(minGroundwaterDepthMonth)*30.0) - 15.0)) *
                     3.14159265358979/180.0);

  double groundwaterDepth = meanGroundwaterDepth + (sinus*groundwaterAmplitude);

  if (groundwaterDepth < 0.0)
    groundwaterDepth = 20.0;

  return groundwaterDepth;
}

//----------------------------------------------------------------------------

/**
 * @brief Returns mean soil organic C.
 * @param depth_m
 */
//Kohlenstoffgehalt 0-depth [% kg C / kg Boden]
double MonicaModel::avgCorg(double depth_m) const
{
  double lsum = 0, sum = 0;
  int count = 0;

  for(int i = 0, nols = _generalParams.ps_NumberOfLayers(); i < nols; i++)
  {
    count++;
    sum +=_soilColumn[i].vs_SoilOrganicCarbon(); //[kg C / kg Boden]
    lsum += _soilColumn[i].vs_LayerThickness;
    if(lsum >= depth_m)
      break;
  }

  return sum / double(count) * 100.0;
}



/**
 * @brief Returns the soil moisture up to 90 cm depth
 * @return water content
 */
//Bodenwassergehalt 0-90cm [%nFK]
double MonicaModel::mean90cmWaterContent() const
{
  return _soilMoisture.meanWaterContent(0.9);
}

double MonicaModel::meanWaterContent(int layer, int number_of_layers) const
{
  return _soilMoisture.meanWaterContent(layer, number_of_layers);
}

/**
 * @brief Returns the N content up to given depth.
 *
 *@return N content
 *
 */
//Boden-Nmin-Gehalt 0-90cm am 31.03. [kg N/ha]
double MonicaModel::sumNmin(double depth_m) const
{
  double lsum = 0, sum = 0;
  int count = 0;

  for(int i = 0, nols = _generalParams.ps_NumberOfLayers(); i < nols; i++)
  {
    count++;
    sum += _soilColumn[i].get_SoilNmin(); //[kg N m-3]
    lsum += _soilColumn[i].vs_LayerThickness;
    if(lsum >= depth_m)
      break;
  }

  return sum / double(count) * lsum * 10000;
}

/**
 * Returns accumulation of soil nitrate for 90cm soil at 31.03.
 * @param depth Depth of soil
 * @return Accumulated nitrate
 */
double
MonicaModel::sumNO3AtDay(double depth_m) const
{
  double lsum = 0, sum = 0;
  int count = 0;

  for(int i = 0, nols = _generalParams.ps_NumberOfLayers(); i < nols; i++)
  {
    count++;
    sum += _soilColumn[i].get_SoilNO3(); //[kg m-3]
    lsum += _soilColumn[i].vs_LayerThickness;
    if(lsum >= depth_m)
      break;
  }

  return sum;
}

//Grundwasserneubildung[mm Wasser]
double MonicaModel::groundWaterRecharge() const
{
  return _soilMoisture.get_GroundwaterRecharge();
}

//N-Auswaschung[kg N/ha]
double MonicaModel::nLeaching() const {
  return _soilTransport.get_NLeaching();//[kg N ha-1]
}

/**
 * Returns sum of soiltemperature in given number of soil layers
 * @param layers Number of layers that should be added.
 * @return Soil temperature sum [°C]
 */
double MonicaModel::sumSoilTemperature(int layers) const
{
  return _soilColumn.sumSoilTemperature(layers);
}

/**
 * Returns maximal snow depth during simulation
 * @return
 */
double
MonicaModel::maxSnowDepth() const
{
  return _soilMoisture.getMaxSnowDepth();
}

/**
 * Returns sum of all snowdepth during whole simulation
 * @return
 */
double MonicaModel::getAccumulatedSnowDepth() const
{
  return _soilMoisture.getAccumulatedSnowDepth();
}

/**
 * Returns sum of frost depth during whole simulation.
 * @return Accumulated frost depth
 */
double MonicaModel::getAccumulatedFrostDepth() const
{
  return _soilMoisture.getAccumulatedFrostDepth();
}

/**
 * Returns average soil temperature of first 30cm soil.
 * @return Average soil temperature of organic layers.
 */
double MonicaModel::avg30cmSoilTemperature() const
{
  double nols = 3;
  double accu_temp = 0.0;
  for (int layer=0; layer<nols; layer++)
    accu_temp+=_soilColumn.soilLayer(layer).get_Vs_SoilTemperature();

  return accu_temp / nols;
}

/**
 * Returns average soil moisture concentration in soil in a defined layer.
 * Layer is specified by start end end of soil layer.
 *
 * @param start_layer
 * @param end_layer
 * @return Average soil moisture concentation
 */
double MonicaModel::avgSoilMoisture(int start_layer, int end_layer) const
{
  int num=0;
  double accu = 0.0;
  for (int i=start_layer; i<end_layer; i++)
  {
    accu+=_soilColumn.soilLayer(i).get_Vs_SoilMoisture_m3();
    num++;
  }
  return accu/num;
}

/**
 * Returns mean of capillary rise in a set of layers.
 * @param start_layer First layer to be included
 * @param end_layer Last layer, is not included;
 * @return Average capillary rise [mm]
 */
double MonicaModel::avgCapillaryRise(int start_layer, int end_layer) const
{
  int num=0;
  double accu = 0.0;
  for (int i=start_layer; i<end_layer; i++)
  {
    accu+=_soilMoisture.get_CapillaryRise(i);
    num++;
  }
  return accu/num;
}

/**
 * @brief Returns mean percolation rate
 * @param start_layer
 * @param end_layer
 * @return Mean percolation rate [mm]
 */
double MonicaModel::avgPercolationRate(int start_layer, int end_layer) const
{
  int num=0;
  double accu = 0.0;
  for (int i=start_layer; i<end_layer; i++)
  {
    accu+=_soilMoisture.get_PercolationRate(i);
    num++;
  }
  return accu/num;
}

/**
 * Returns sum of all surface run offs at this point in simulation time.
 * @return Sum of surface run off in [mm]
 */
double MonicaModel::sumSurfaceRunOff() const
{
  return _soilMoisture.get_SumSurfaceRunOff();
}

/**
 * Returns surface runoff of current day [mm].
 */
double MonicaModel::surfaceRunoff() const
{
  return _soilMoisture.get_SurfaceRunOff();
}

/**
 * Returns evapotranspiration [mm]
 * @return
 */
double MonicaModel::getEvapotranspiration() const
{
  if (_currentCropGrowth!=0)
    return _currentCropGrowth->get_RemainingEvapotranspiration();

  return 0.0;
}

/**
 * Returns actual transpiration
 * @return
 */
double MonicaModel::getTranspiration() const
{
  if (_currentCropGrowth!=0)
    return _currentCropGrowth->get_ActualTranspiration();

  return 0.0;
}

/**
 * Returns actual evaporation
 * @return
 */
double MonicaModel::getEvaporation() const
{
  if (_currentCropGrowth!=0)
    return _currentCropGrowth->get_EvaporatedFromIntercept();

  return 0.0;
}

double MonicaModel::getETa() const
{
  return _soilMoisture.get_Evapotranspiration();
}

/**
 * Returns sum of evolution rate in first three layers.
 * @return
 */
double MonicaModel::get_sum30cmSMB_CO2EvolutionRate() const
{
  double sum = 0.0;
  for (int layer=0; layer<3; layer++) {
    sum+=_soilOrganic.get_SMB_CO2EvolutionRate(layer);
  }

  return sum;
}

/**
 * Returns volatilised NH3
 * @return
 */
double MonicaModel::getNH3Volatilised() const
{
  return _soilOrganic.get_NH3_Volatilised();
}

/**
 * Returns accumulated sum of all volatilised NH3 in simulation time.
 * @return
 */
double MonicaModel::getSumNH3Volatilised() const
{
//  cout << "NH4Vol: " << _soilOrganic.get_sumNH3_Volatilised() << endl;
  return _soilOrganic.get_SumNH3_Volatilised();
}

/**
 * Returns sum of denitrification rate in first 30cm soil.
 * @return Denitrification rate [kg N m-3 d-1]
 */
double MonicaModel::getsum30cmActDenitrificationRate() const
{
  double sum=0.0;
  for (int layer=0; layer<3; layer++) {
//    cout << "DENIT: " << _soilOrganic.get_ActDenitrificationRate(layer) << endl;
    sum+=_soilOrganic.get_ActDenitrificationRate(layer);
  }

  return sum;
}

