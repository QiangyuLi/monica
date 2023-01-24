/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
Authors: 
Michael Berg <michael.berg@zalf.de>

Maintainers: 
Currently maintained by the authors.

This file is part of the MONICA model. 
Copyright (C) Leibniz Centre for Agricultural Landscape Research (ZALF)
*/

#include <stdio.h>
#include <iostream>
#include <fstream>
#include <string>
#include <tuple>

#include "json11/json11.hpp"
#include "json11/json11-helper.h"
#include "zeromq/zmq-helper.h"
#include "tools/helper.h"
//#include "db/abstract-db-connections.h"
#include "tools/debug.h"
#include "../run/run-monica.h"
#include "../run/run-monica-zmq.h"
#include "../core/monica-model.h"
#include "climate/climate-file-io.h"
#include "soil/conversion.h"
#include "env-json-from-json-config.h"
#include "tools/algorithms.h"
#include "../io/csv-format.h"
#include "monica-zmq-defaults.h"

using namespace std;
using namespace monica;
using namespace Tools;
using namespace json11;

string appName = "monica-zmq-run";
string version = "2.0.0-beta";

int main(int argc, char** argv)
{
  setlocale(LC_ALL, "");
  setlocale(LC_NUMERIC, "C");

  //use a possibly non-default db-connections.ini
  //Db::dbConnectionParameters("db-connections.ini");

  bool debug = false, debugSet = false;
  string startDate, endDate;
  string pathToOutput;
  string pathToOutputFile;
  bool writeOutputFile = false;
  string address = defaultInputAddress;
  int port = defaultInputPort;
  string pathToSimJson = "./sim.json", crop, site, climate;
  string dailyOutputs;
  bool cesMode = false;

  auto printHelp = [=]()
  {
    cout
      << appName << " [options] path-to-sim-json" << endl
      << endl
      << " -h   | --help ... this help output" << endl
      << " -v   | --version ... outputs MONICA version" << endl
      << endl
      << " -d   | --debug ... show debug outputs" << endl
      << " -a   | --address (PROXY-)ADDRESS (default: " << address << ") ... connect client to give IP address" << endl
      << " -p   | --port (PROXY-)PORT (default: " << port << ") ... run server/connect client on/to given port" << endl
      //<< " -sd  | --start-date ISO-DATE (default: start of given climate data) ... date in iso-date-format yyyy-mm-dd" << endl
      //<< " -ed  | --end-date ISO-DATE (default: end of given climate data) ... date in iso-date-format yyyy-mm-dd" << endl
      << " -w   | --write-output-files ... write MONICA output files (rmout, smout)" << endl
      << " -op  | --path-to-output DIRECTORY (default: .) ... path to output directory" << endl
      << " -o   | --path-to-output-file FILE ... path to output file" << endl
      //<< " -do  | --daily-outputs [LIST] (default: value of key 'sim.json:output.daily') ... list of daily output elements" << endl
      << " -c   | --path-to-crop FILE (default: ./crop.json) ... path to crop.json file" << endl
      << " -s   | --path-to-site FILE (default: ./site.json) ... path to site.json file" << endl
      << " -w   | --path-to-climate FILE (default: ./climate.csv) ... path to climate.csv" << endl
      << " -ces  | --create-env-server ... start monica-zmq-run as a server on given port and create JSON env for clients" << endl;
  };

  zmq::context_t context(1);
  zmq::socket_t cesSocket(context, ZMQ_REP);
  
  if(argc <= 1)
  {
    printHelp();
    return 0;
  }
    
  for(auto i = 1; i < argc; i++)
  {
    string arg = argv[i];
    if(arg == "-d" || arg == "--debug")
      debug = debugSet = true;
    else if((arg == "-a" || arg == "--address")
            && i + 1 < argc)
      address = argv[++i];
    else if((arg == "-p" || arg == "--port")
            && i + 1 < argc)
      port = stoi(argv[++i]);
    //else if((arg == "-sd" || arg == "--start-date")
    //    && i+1 < argc)
    //	startDate = argv[++i];
    //else if((arg == "-ed" || arg == "--end-date")
    //    && i+1 < argc)
    //	endDate = argv[++i];
    else if((arg == "-op" || arg == "--path-to-output")
          && i+1 < argc)
      pathToOutput = argv[++i];
    else if((arg == "-o" || arg == "--path-to-output-file")
            && i + 1 < argc)
      pathToOutputFile = argv[++i];
    //else if((arg == "-do" || arg == "--daily-outputs")
    //				&& i + 1 < argc)
    //	dailyOutputs = argv[++i];
    else if((arg == "-c" || arg == "--path-to-crop")
          && i+1 < argc)
      crop = argv[++i];
    else if((arg == "-s" || arg == "--path-to-site")
          && i+1 < argc)
      site = argv[++i];
    else if((arg == "-w" || arg == "--path-to-climate")
          && i+1 < argc)
      climate = argv[++i];
    else if(arg == "-h" || arg == "--help")
      printHelp(), exit(0);
    else if(arg == "-v" || arg == "--version")
      cout << appName << " version " << version << endl, exit(0);
    else if(arg == "-ces" || arg == "--create-env-server")
      cesMode = true;
    else
      pathToSimJson = argv[i];
  }

  
  if(cesMode)
  {
    try
    {
      cesSocket.bind("tcp://*:" + to_string(port));
    }
    catch(zmq::error_t e)
    {
      cerr << "Couldn't bind zmq (create env server)-socket to address: tcp://*:" + to_string(port);
      exit(1);
    }

    while(cesMode)
    {
      try
      {
        Msg msg = receiveMsg(cesSocket);

        string msgType = msg.type();
        if(msgType == "finish")
        {
          //only send reply when not in pipeline configuration
          J11Object resultMsg;
          resultMsg["type"] = "ack";
          try
          {
            s_send(cesSocket, Json(resultMsg).dump());
          }
          catch(zmq::error_t e)
          {
            cerr << "Exception on trying to reply to 'finish' request with 'ack' message on zmq socket with address: tcp://*:" + to_string(port);
            cerr << "! Will finish MONICA process! Error: [" << e.what() << "]" << endl;
          }
          cesSocket.setsockopt(ZMQ_LINGER, 0);
          cesSocket.close();
          break;
        }
        else if(msgType == "CreateEnv")
        {
          Json& fullMsg = msg.json;

          auto env = createEnvJsonFromJsonObjects(
          {{"sim", fullMsg["sim"]}
          ,{"crop", fullMsg["crop"]}
          ,{"site", fullMsg["site"]}});

          try
          {
            s_send(cesSocket, env.dump());
          }
          catch(zmq::error_t e)
          {
            cerr << "Exception on trying to reply to 'CreateEnv' request with 'Env' message on zmq socket with address: tcp://*:" + to_string(port);
            cerr << "! Will continue to receive requests! Error: [" << e.what() << "]" << endl;
          }

        }
      }
      catch(zmq::error_t e)
      {
        cerr << "Exception on trying to receive request message on zmq socket with address: tcp://*:" + to_string(port);
        cerr << "! Will continue to receive requests! Error: [" << e.what() << "]" << endl;
      }
    }
  }
  else
  {
    string pathOfSimJson, simFileName;
    tie(pathOfSimJson, simFileName) = splitPathToFile(pathToSimJson);

    auto simj = readAndParseJsonFile(pathToSimJson);
    if(simj.failure())
      for(auto e : simj.errors)
        cerr << e << endl;
    auto simm = simj.result.object_items();

    //if(!startDate.empty())
    //	simm["start-date"] = startDate;

    //if(!endDate.empty())
    //	simm["end-date"] = endDate;

    if(debugSet)
      simm["debug?"] = debug;

    if(!pathToOutput.empty())
      simm["path-to-output"] = pathToOutput;

    //if(!pathToOutputFile.empty())
    //	simm["path-to-output-file"] = pathToOutputFile;

    simm["sim.json"] = pathToSimJson;

    if(!crop.empty())
      simm["crop.json"] = crop;
    auto pathToCropJson = simm["crop.json"].string_value();
    if(!isAbsolutePath(pathToCropJson))
      simm["crop.json"] = pathOfSimJson + pathToCropJson;

    if(!site.empty())
      simm["site.json"] = site;
    auto pathToSiteJson = simm["site.json"].string_value();
    if(!isAbsolutePath(pathToSiteJson))
      simm["site.json"] = pathOfSimJson + pathToSiteJson;

    if(!climate.empty())
      simm["climate.csv"] = climate;
    auto pathToClimateCSV = simm["climate.csv"].string_value();
    if(!isAbsolutePath(pathToClimateCSV))
      simm["climate.csv"] = pathOfSimJson + pathToClimateCSV;

    /*
    if(!dailyOutputs.empty())
    {
      auto outm = simm["output"].object_items();
      string err;
      J11Array daily;

      string trimmedDailyOutputs = trim(dailyOutputs);
      if(trimmedDailyOutputs.front() == '[')
        trimmedDailyOutputs.erase(0, 1);
      if(trimmedDailyOutputs.back() == ']')
        trimmedDailyOutputs.pop_back();

      for(auto el : splitString(trimmedDailyOutputs, ",", make_pair("[", "]")))
      {
        if(trim(el).at(0) == '[')
        {
          J11Array a;
          auto es = splitString(trim(el, "[]"), ",");
          if(es.size() >= 1)
            a.push_back(es.at(0));
          if(es.size() >= 3)
            a.push_back(stoi(es.at(1))), a.push_back(stoi(es.at(2)));
          if(es.size() >= 4)
            a.push_back(es.at(3));
          daily.push_back(a);
        }
        else
          daily.push_back(el);
      }
      outm["daily"] = daily;
      simm["output"] = outm;
    }
    */

    map<string, string> ps;
    ps["sim-json-str"] = json11::Json(simm).dump();
    ps["crop-json-str"] = printPossibleErrors(readFile(simm["crop.json"].string_value()), activateDebug);
    ps["site-json-str"] = printPossibleErrors(readFile(simm["site.json"].string_value()), activateDebug);
    //ps["path-to-climate-csv"] = simm["climate.csv"].string_value();

    auto env = createEnvJsonFromJsonStrings(ps);
    activateDebug = env["debugMode"].bool_value();

    if(activateDebug)
      cout << "starting MONICA with JSON input files" << endl;

    Json out_ = sendZmqRequestMonicaFull(&context, string("tcp://") + address + ":" + to_string(port), env);
    Output output(out_);

    if(pathToOutputFile.empty() && simm["output"]["write-file?"].bool_value())
      pathToOutputFile = fixSystemSeparator(simm["path-to-output"].string_value() + "/"
                                            + simm["output"]["file-name"].string_value());

    writeOutputFile = !pathToOutputFile.empty();

    ofstream fout;
    if(writeOutputFile)
    {
      string path, filename;
      tie(path, filename) = splitPathToFile(pathToOutputFile);
      ensureDirExists(path);
      fout.open(pathToOutputFile);
      if(fout.fail())
      {
        cerr << "Error while opening output file \"" << pathToOutputFile << "\"" << endl;
        writeOutputFile = false;
      }
    }

    ostream& out = writeOutputFile ? fout : cout;

    string csvSep = simm["output"]["csv-options"]["csv-separator"].string_value();
    bool includeHeaderRow = simm["output"]["csv-options"]["include-header-row"].bool_value();
    bool includeUnitsRow = simm["output"]["csv-options"]["include-units-row"].bool_value();
    bool includeAggRows = simm["output"]["csv-options"]["include-aggregation-rows"].bool_value();

    for(const auto& d : output.data)
    {
      out << "\"" << replace(d.origSpec, "\"", "") << "\"" << endl;
      writeOutputHeaderRows(out, d.outputIds, csvSep, includeHeaderRow, includeUnitsRow, includeAggRows);
      writeOutput(out, d.outputIds, d.results, csvSep);
      out << endl;

    }

    if(writeOutputFile)
      fout.close();

    if(activateDebug)
      cout << "finished MONICA" << endl;
  }
  
  return 0;
}
