#include"uniInclude.h"
#include"config.h"

#include"fstream"
#define FILE_NOT_FOUND 1
bool init_config() {
	std::ifstream fin("config.conf");
	if(!fin.is_open()){
		std::ofstream fout("config.conf");
		fout.close();
	}
	else{
		fin.close();
	}
	return true;
}