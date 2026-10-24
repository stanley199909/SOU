// A compact static stage manifest shared by Forge and StageEditor.
#pragma once
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
namespace OutdoorStage {
struct Entry { std::string key,path; float x=0,y=0,z=0,yaw=0,scale=.01f; };
inline std::vector<Entry> Read() {
 std::vector<Entry> entries; std::ifstream file("Assets/outdoor_stage.txt"); std::string line;
 while(std::getline(file,line)) { if(line.empty() || line[0]=='#') continue;
  Entry e; std::istringstream row(line);
  if(row>>e.key>>e.path>>e.x>>e.y>>e.z>>e.yaw>>e.scale)
   if(e.key.find("StOutdoor")==0 && e.scale>0) entries.push_back(e);
 }
 return entries;
}
inline bool IsOutdoor(const std::string& key) { return key.find("StOutdoor")==0; }
}
