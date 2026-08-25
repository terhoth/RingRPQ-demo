/***
BSD 2-Clause License

Copyright (c) 2018, Adrián
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**/


//
// Created by Adrián on 13/6/22.
//

// Updated by Terho Thorel in August 2026 to work with newer YAGO version 4.6

#include <iostream>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <regex>
#include "../Config.hpp"

inline bool valid_line(std::string &line){
    return !line.empty() && line[0] != '@' && line[0] != '#';
}
inline bool is_prefix_definition(std::string &line){
    return !line.empty() && line[0] == '@';
}


inline void fix_data(std::string &d){
    if(d[0] != '<'){
        d = '<' + d + '>';
    }
}

bool sortbysec(const std::pair<std::string, uint64_t> &a,
               const std::pair<std::string, uint64_t> &b)
{
    return (a.second < b.second);
}

bool sortby_triple(const spo_triple &a, const spo_triple &b)
{
    if(get<0>(a) == get<0>(b)){
        if(get<1>(a) == get<1>(b)){
            return get<2>(a) < get<2>(b);
        }
        return get<1>(a) < get<1>(b);
    }
    return get<0>(a) < get<0>(b);
}

void map_to_file(const std::string &file_name, std::unordered_map<std::string, uint64_t> &map){

    std::vector<std::pair<std::string, uint64_t>> pairs(map.begin(), map.end());
    map.clear();
    std::sort(pairs.begin(), pairs.end(), sortbysec);
    std::ofstream out(file_name);
    for(const auto &pair : pairs){
        out << pair.second << " " << pair.first << std::endl;
    }
    out.close();
}

void vector_to_file(const std::string &file_name, std::vector<spo_triple> &vec){

    std::ofstream out(file_name);
    for(const auto &spo : vec){
        out <<  get<0>(spo) << " " << get<1>(spo) << " " << get<2>(spo) << std::endl;
    }
    out.close();
}

bool is_literal(const std::string &uri){
    return (uri.find('\"') == 0);
}

bool is_uri(const std::string &uri){
    return (uri.find("<http://") == 0);
}

std::string get_prefix(const std::string &uri){
    auto pos = uri.find(':');
    if(pos!=std::string::npos){
        return uri.substr(1, pos);
    }else{
        return "";
    }
}

void get_id_uri(const std::string &line, std::string &id, std::string &uri){
    auto pos = line.find(" ");
    id = line.substr(0, pos);
    uri = line.substr(pos+1);
}

std::pair<bool, std::string> fix_prefix(const std::string &uri, const std::string &prefix, const std::string &p_uri){
    auto pos = uri.find(prefix);
    if(pos == std::string::npos) return {false, ""};
    return {true, "<" + std::regex_replace(uri, std::regex(prefix), p_uri) + ">"};
}

std::pair<bool, std::string> fix_literal_prefix(const std::string &uri, const std::string &prefix, const std::string &p_uri){
    auto pos = uri.find(prefix);
    if(pos == std::string::npos) return {false, ""};
    auto end_literal = uri.find('\"', 1);
    if(pos < end_literal) return {false, ""};
    auto new_uri = std::regex_replace(uri, std::regex(prefix), p_uri);
    auto p = new_uri.find("http://");
    auto s1 = new_uri.substr(0, p);
    auto s2 = new_uri.substr(p);
    return {true,  s1 + "<" + s2 + ">"};
}

std::string base(const std::string &uri, const std::string &uri_base){
    auto str_aux = uri.substr(1, uri.size()-2);
    return "<" + uri_base + str_aux + ">";
}


std::string transform(const std::string &uri,
                      const std::unordered_map<std::string, std::string>& prefixes,
                      const std::string &uri_base){

    if(is_uri(uri)) {
        return uri;
    }else if (is_literal(uri)){
        for(const auto &p : prefixes){
            auto fix = fix_literal_prefix(uri, p.first, p.second);
            if(fix.first){
                return fix.second;
            }
        }
        return uri;
    }else{
        for(const auto &p : prefixes){
            auto fix = fix_prefix(uri, p.first, p.second);
            if(fix.first){
				//std::cout << "fix prefix: " << uri << " --> " << fix.second << std::endl;
                return fix.second;
            }
        }
    }
    return base(uri, uri_base);
}

int main(int argc, char **argv){

    if(argc < 3)
	{
        std::cout << argv[0] << " <input.ttl> <output> <limit>" << std::endl;
        std::cout << "<input.ttl> Turtle file." << std::endl;
        std::cout << "<output> Output file that stores the triples as ids." << std::endl;
        std::cout << "<limit> Optional number of lines to read only part of the input." << std::endl;
        return EXIT_FAILURE;
    }
	unsigned int max_lines = 0;
	if (argc == 4)
	{
		max_lines = std::stoi(argv[3]);
	}

    std::string input  = argv[1];
    std::string output = argv[2];

    std::string uri_base = "http://yago-knowledge.org/resource/";
/*
    std::pair<std::string, std::string> rdf = {"rdf:", "http://www.w3.org/1999/02/22-rdf-syntax-ns#"};
    std::pair<std::string, std::string> rdfs = {"rdfs:", "http://www.w3.org/2000/01/rdf-schema#"};
    std::pair<std::string, std::string> owl = {"owl:", "http://www.w3.org/2002/07/owl#"};
    std::pair<std::string, std::string> xsd = {"xsd:", "http://www.w3.org/2001/XMLSchema#"};
    std::pair<std::string, std::string> skos = {"skos:", "http://www.w3.org/2004/02/skos/core#"};

    std::vector<std::pair<std::string, std::string>> prefixes = {rdf, rdfs, owl, xsd, skos};
*/
	std::unordered_map<std::string, std::string> prefixes;

    std::ifstream in(input);
	if (!in.is_open())
	{
		std::cout << "Failed to open file: " << input << std::endl;
		return EXIT_FAILURE;
	}
	std::cout << "Opened " << input << " succesfully" << std::endl;

    std::string line;
    std::unordered_map<std::string, uint64_t> so_map;
    std::unordered_map<std::string, uint64_t> p_map;
    uint64_t so_new_id = 1;
    uint64_t p_new_id = 1;
    std::vector<spo_triple> D;
	unsigned int n_of_lines = 0;
    while (std::getline(in, line)) {
		++n_of_lines;
		if (n_of_lines > max_lines and max_lines > 0) break;
		if (n_of_lines % 1000000 == 0)
		{
			std::cout << "..." << std::endl;
			std::cout << n_of_lines / 1000000 << " million lines processed so far" << std::endl;
			std::cout << "current line:" << std::endl;
			std::cout << line << std::endl;
		}

		if (is_prefix_definition(line)) {
			std::istringstream iss(line);
			std::string tmp, prefix_abbr, prefix_uri;
			iss >> tmp >> prefix_abbr >> prefix_uri;
			prefix_uri = prefix_uri.substr(1, prefix_uri.length()-2);
			prefixes[prefix_abbr] = prefix_uri; 
			//std::cout << "@prefix " << prefix_abbr << " " << prefix_uri << std::endl;
		}
        else if (valid_line(line)) {
            std::istringstream iss(line);
            std::string s, p, o, point;
            uint64_t s_id, p_id, o_id;
            std::getline( iss, s, '\t' );
            std::getline( iss, p, '\t' );
            std::getline( iss, o, '\t' );
            std::getline( iss, point, '\t' );
			if (is_literal(o)) continue;
			//std::cout << s << " " << p << " " << o << " ?" << std::endl;
            s = transform(s, prefixes, uri_base);
            p = transform(p, prefixes, uri_base);
            o = transform(o, prefixes, uri_base);
            //std::cout << s << " " << p << " " << o  << " ." << std::endl;
            auto it_so = so_map.find(s);
            if(it_so != so_map.end()){
                s_id = it_so->second;
            }else{
                s_id = so_new_id++;
                so_map.insert({s, s_id});
            }
            auto it_p = p_map.find(p);
            if(it_p != p_map.end()){
                p_id = it_p->second;
            }else{
                p_id = p_new_id++;
                p_map.insert({p, p_id});
            }
            it_so = so_map.find(o);
            if(it_so != so_map.end()){
                o_id = it_so->second;
            }else{
                o_id = so_new_id++;
                so_map.insert({o, o_id});
            }
            D.emplace_back(spo_triple{s_id, p_id, o_id});
        }
    }
    in.close();
	std::cout << std::endl << "Data read! Now writing..." << std::endl;
    std::sort(D.begin(), D.end(), sortby_triple);
    //Build file
    std::cout << "Writing triples to " << output << std::endl;
    vector_to_file(output, D);
    //Build files .P and .SO
    std::cout << "Writing subjects/objects to " << output + ".SO" << std::endl;
    map_to_file(output + ".SO", so_map);
    std::cout << "Writing predicates to " << output + ".P" << std::endl;
    map_to_file(output + ".P", p_map);
	std::cout << "Done!" << std::endl;

    return EXIT_SUCCESS;
}
