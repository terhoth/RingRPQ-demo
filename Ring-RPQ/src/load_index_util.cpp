/*
 * File: load_index_util.cpp
 * Author: Terho Thorel
 * Description: 
 * 	 A function for loading attribute type information from a .dat.A file.
 *   Moved to a separate file to avoid repeting the same code
 *   in build-index-attr.cpp, query-index-attr.cpp and test-correctness.cpp.
 *
 */

#ifndef LOAD_INDEX_UTIL
#define LOAD_INDEX_UTIL

#include <vector>
#include <unordered_map>
#include <tuple>
#include "attr_info.hpp"
#include "parse_query_attr.cpp"

// Reads the attribute type file related to index_file,
// and collects attribute names, ids and types to attr_map.
// Optionally also constructs vectors from edge and node attributes.
// Returns the total number of attributes by type (string, integer, float)
std::tuple<unsigned, unsigned, unsigned> 
load_attribute_info(const std::string& index_file, 
                    std::unordered_map<std::string, AttrInfo>& attr_map, 
                    std::vector<AttrInfo>* edge_infos = nullptr,
                    std::vector<AttrInfo>* node_infos = nullptr)
{
	int n_of_str_attrs = 0; 
	int n_of_int_attrs = 0; 
	int n_of_flt_attrs = 0; 

	// create an attribute to hold node labels
	AttrInfo nodelabel_info = { 0, AttrInfo::Node, AttrInfo::Str };
	attr_map.insert({NODELABEL_ATTR, nodelabel_info});
	if (node_infos != nullptr) node_infos->push_back(nodelabel_info);
	n_of_str_attrs++;


	std::ifstream A_ifs((index_file + ".A").c_str());
	if (A_ifs.is_open())
	{
		AttrInfo current_attr_info = { 1, AttrInfo::Edge, AttrInfo::Str };
		std::string attr_name;
		while (A_ifs >> attr_name) 
		{
			// header line?
			if (attr_name == "STRING")
			{
				current_attr_info.type = AttrInfo::Str;
			}
			else if (attr_name == "INTEGER")
			{
				current_attr_info.type = AttrInfo::Int;
			}
			else if (attr_name == "FLOAT")
			{
				current_attr_info.type = AttrInfo::Flt;
			}
			else if (attr_name == "NODE")
			{
				current_attr_info.location = AttrInfo::Node;
			}
			else if (attr_name == "EDGE")
			{
				current_attr_info.location = AttrInfo::Edge;
			}
			else // actual attribute name
			{
				// map the attribute's name to id
				if (attr_map.find(attr_name) == attr_map.end())
				{
					if (current_attr_info.type == AttrInfo::Str) 
					{ 
						current_attr_info.id = n_of_str_attrs;
						n_of_str_attrs++; 
					}
					else if (current_attr_info.type == AttrInfo::Int) 
					{ 
						current_attr_info.id = n_of_int_attrs;
						n_of_int_attrs++; 
					}
					else if (current_attr_info.type == AttrInfo::Flt) 
					{ 
						current_attr_info.id = n_of_flt_attrs;
						n_of_flt_attrs++; 
					}
					attr_map.insert({attr_name, current_attr_info});
				}

				// store attribute info to be used when reading the data
				if (current_attr_info.location == AttrInfo::Edge)
				{	   
					if (edge_infos != nullptr) edge_infos->push_back(current_attr_info);
				}
				else
				{
					if (node_infos != nullptr) node_infos->push_back(current_attr_info);
				}
			}
		}
	}

	return std::tie(n_of_str_attrs, n_of_int_attrs, n_of_flt_attrs);
}


#endif
