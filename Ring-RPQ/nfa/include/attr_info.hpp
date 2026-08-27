/*
 * File: attr_info.hpp
 * Author: Terho Thorel
 * Description: helper structures to keep track of attribute types 
 * 	as well as store and test attribute conditions
 */

#ifndef ATTR_INFO_HPP
#define ATTR_INFO_HPP

#include <cmath>
#include <cstdint>
#include <string>

// used in .dat files to indicate missing attribute values
const std::string NULL_VALUE = "_";

// Stores information about an attribute
typedef struct
{ 
	uint64_t id;
	enum { Edge, Node } location; 
	enum { None, Str, Int, Flt } type;
} AttrInfo;

// A function object used to test attributes.
// Acts as a polymorphic interface so that all acceptors can be put into a single STL container.
class Acceptor
{
public:
	Acceptor(): // by default the acceptor returns always true
		str_f([](std::string){ return true; }),
		int_f([](int){ return true; }),
		flt_f([](float){ return true; }) {};

	Acceptor(std::function<bool(std::string)> f): str_f(f) {};
	Acceptor(std::function<bool(int)> f): int_f(f) {};
	Acceptor(std::function<bool(float)> f): flt_f(f) {};

	bool operator()(std::string str) const { return str_f(str); };
	bool operator()(int num) const { return int_f(num); };
	bool operator()(float num) const { return flt_f(num); };
	bool operator()() const { return accepts_null; };

	// Constructs the acceptor given the attribute's type as an AttrInfo struct, as well as 
	// strings representing the comparator relation to use and the value to compare against.
	Acceptor(const AttrInfo& attr, const std::string& relation, const std::string& value_str)
	{
		if (value_str == NULL_VALUE)
		{
			if (relation == "=" or relation == "==")
			{
				// will accept only null values	
				accepts_null = true;
			}
			else if (relation == "!=" or relation == "<>" or relation == "><")
			{
				// will accept only non-null values
				str_f = [](std::string){ return true; };
				int_f = [](int){ return true; };
				flt_f = [](float){ return true; };
			}
		}
		else if (attr.type == AttrInfo::Str) 
		{
			// string attribute
			if (relation == "=" or relation == "==")
			{
				str_f = [value_str](std::string str) { return str == value_str; };
			}
			else if (relation == "!=" or relation == "<>" or relation == "><")
			{
				str_f = [value_str](std::string str) { return str != value_str; };
			}
			else if (relation == ">")
			{
				// lexicographic comparison
				str_f = [value_str](std::string str) { return str > value_str; };
			}
			else if (relation == ">=" or relation == "=>")
			{
				str_f = [value_str](std::string str) { return str >= value_str; };
			}
			else if (relation == "<")
			{
				str_f = [value_str](std::string str) { return str < value_str; };
			}
			else if (relation == "<=" or relation == "=<")
			{
				str_f = [value_str](std::string str) { return str <= value_str; };
			}
			else if (relation == "BEGINS" or relation == "STARTS")
			{
				// tests whether str_value is a prefix of attribute value
				str_f = [value_str](std::string str) { return str.size() >= value_str.size() 
				                                       && str.substr(0, value_str.size()) == value_str; };
			}
			else if (relation == "ENDS")
			{
				// tests whether str_value is a postfix of attribute value
				str_f = [value_str](std::string str) { return str.size() >= value_str.size() 
				                                       && str.substr(str.size() - value_str.size()) == value_str; };
			}
			else if (relation == "PREFIX")
			{
				// tests whether attribute value is a prefix of value_str
				str_f = [value_str](std::string str) { return str.size() <= value_str.size() 
				                                       && value_str.substr(0, str.size()) == str; };
			}
			else if (relation == "POSTFIX" or relation == "SUFFIX")
			{
				// tests whether attribute value is a postfix of value_str
				str_f = [value_str](std::string str) { return str.size() <= value_str.size() 
				                                       && value_str.substr(value_str.size() - str.size()) == str; };
			}
		}
		else if (attr.type == AttrInfo::Int)
		{
			// integer attribute
			int value_int = stoi(value_str);
			if (relation == "=" or relation == "==")
			{
				int_f = [value_int](int num) { return num == value_int; };
			}
			else if (relation == "!=" or relation == "<>" or relation == "><")
			{
				int_f = [value_int](int num) { return num != value_int; };
			}
			else if (relation == ">")
			{
				int_f = [value_int](int num) { return num > value_int; };
			}
			else if (relation == ">=")
			{ 
				int_f = [value_int](int num) { return num >= value_int; };
			}
			else if (relation == "<")
			{
				int_f = [value_int](int num) { return num < value_int; };
			}
			else if (relation == "<=")
			{
				int_f = [value_int](int num) { return num <= value_int; };
			}		
		}		
		else if (attr.type == AttrInfo::Flt)
		{
			// float attribute 
			float value_flt = stof(value_str);
			if (relation == "=" or relation == "==")
			{
				flt_f = [value_flt](float num) { return num == value_flt; };
			}
			else if (relation == "!=" or relation == "<>" or relation == "><")
			{
				flt_f = [value_flt](float num) { return num != value_flt; };
			}
			else if (relation == ">")
			{
				flt_f = [value_flt](float num) { return num > value_flt; };
			}
			else if (relation == ">=")
			{ 
				flt_f = [value_flt](float num) { return num >= value_flt; };
			}
			else if (relation == "<")
			{
				flt_f = [value_flt](float num) { return num < value_flt; };
			}
			else if (relation == "<=")
			{
				flt_f = [value_flt](float num) { return num <= value_flt; };
			}		
			else if (relation.length() >= 5 and (relation.substr(0, 5) == "ROUND" or relation.substr(0, 5) == "APPRO"))
			{
				// tests equality up to a given precision, e.g. ROUND2 for two decimals
				int precision = (relation.back() - '0'); // distance in ASCII
				if (precision < 0 or precision > 9) precision = 0; // default to no decimals
				if (precision > 7) precision = 7; // c++ float precision is 7 decimals
				// calculate the precisionth power of ten
				int power_of_10 = 1;
				while (precision > 0) 
				{ 
					power_of_10 *= 10; 
					precision--; 
				}
				flt_f = [value_flt, power_of_10](float num) 
					{ return std::round(power_of_10 * (num - value_flt)) == 0; };
			}
		}
	}

private:
	std::function<bool(std::string)> str_f = [](std::string){ return false; };
	std::function<bool(int)> int_f = [](int){ return false; };
	std::function<bool(float)> flt_f = [](float){ return false; };
	bool accepts_null = false; // when true, accepts if no arguments are given
};


#endif
