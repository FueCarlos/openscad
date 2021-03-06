/*
 *  OpenSCAD (www.openscad.org)
 *  Copyright (C) 2009-2011 Clifford Wolf <clifford@clifford.at> and
 *                          Marius Kintel <marius@kintel.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  As a special exception, you have permission to link this program
 *  with the CGAL library and distribute executables, as long as you
 *  follow the requirements of the GNU GPL in regard to all of the
 *  software in the executable aside from CGAL.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "function.h"
#include "expression.h"
#include "evalcontext.h"
#include "builtin.h"
#include <sstream>
#include <ctime>
#include "mathc99.h"
#include <limits>
#include <algorithm>
#include "stl-utils.h"
#include "printutils.h"
#include <boost/foreach.hpp>

#include <boost/math/special_functions/fpclassify.hpp>
using boost::math::isnan;
using boost::math::isinf;

/*
 Random numbers

 Newer versions of boost/C++ include a non-deterministic random_device and
 auto/bind()s for random function objects, but we are supporting older systems.
*/

#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_real.hpp>
/*Unicode support for string lengths and array accesses*/
#include <glib.h>

#ifdef __WIN32__
#include <process.h>
int process_id = _getpid();
#else
#include <sys/types.h>
#include <unistd.h>
int process_id = getpid();
#endif

boost::mt19937 deterministic_rng;
boost::mt19937 lessdeterministic_rng( std::time(0) + process_id );

AbstractFunction::~AbstractFunction()
{
}

Value AbstractFunction::evaluate(const Context*, const EvalContext *evalctx) const
{
	(void)evalctx; // unusued parameter
	return Value();
}

std::string AbstractFunction::dump(const std::string &indent, const std::string &name) const
{
	std::stringstream dump;
	dump << indent << "abstract function " << name << "();\n";
	return dump.str();
}

Function::~Function()
{
	delete expr;
}

Value Function::evaluate(const Context *ctx, const EvalContext *evalctx) const
{
	if (!expr) return Value();
	Context c(ctx);
	c.setVariables(definition_arguments, evalctx);
	return expr->evaluate(&c);
}

std::string Function::dump(const std::string &indent, const std::string &name) const
{
	std::stringstream dump;
	dump << indent << "function " << name << "(";
	for (size_t i=0; i < definition_arguments.size(); i++) {
		const Assignment &arg = definition_arguments[i];
		if (i > 0) dump << ", ";
		dump << arg.first;
		if (arg.second) dump << " = " << *arg.second;
	}
	dump << ") = " << *expr << ";\n";
	return dump.str();
}

BuiltinFunction::~BuiltinFunction()
{
}

Value BuiltinFunction::evaluate(const Context *ctx, const EvalContext *evalctx) const
{
	return eval_func(ctx, evalctx);
}

std::string BuiltinFunction::dump(const std::string &indent, const std::string &name) const
{
	std::stringstream dump;
	dump << indent << "builtin function " << name << "();\n";
	return dump.str();
}

static inline double deg2rad(double x)
{
	return x * M_PI / 180.0;
}

static inline double rad2deg(double x)
{
	return x * 180.0 / M_PI;
}

Value builtin_abs(const Context *, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		const Value &v = evalctx->getArgValue(0);
		if (v.type() == Value::NUMBER)
			return Value(fabs(v.toDouble()));
	}
	return Value();
}

Value builtin_sign(const Context *, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		const Value &v = evalctx->getArgValue(0);
		if (v.type() == Value::NUMBER) {
			register double x = v.toDouble();
			return Value((x<0) ? -1.0 : ((x>0) ? 1.0 : 0.0));
		}
	}
	return Value();
}

Value builtin_rands(const Context *, const EvalContext *evalctx)
{
	size_t n = evalctx->numArgs();
	if (n == 3 || n == 4) {
		const Value &v0 = evalctx->getArgValue(0);
		if (v0.type() != Value::NUMBER) goto quit;
		double min = v0.toDouble();

		const Value &v1 = evalctx->getArgValue(1);
		if (v1.type() != Value::NUMBER) goto quit;
		double max = v1.toDouble();
		if (max < min) {
			register double tmp = min; min = max; max = tmp;
		}
		const Value &v2 = evalctx->getArgValue(2);
		if (v2.type() != Value::NUMBER) goto quit;
		size_t numresults = std::max( 0, static_cast<int>( v2.toDouble() ) );

		bool deterministic = false;
		if (n > 3) {
			const Value &v3 = evalctx->getArgValue(3);
			if (v3.type() != Value::NUMBER) goto quit;
			deterministic_rng.seed( (unsigned int) v3.toDouble() );
			deterministic = true;
		}
		boost::uniform_real<> distributor( min, max );
		Value::VectorType vec;
		if (min==max) { // workaround boost bug
			for (size_t i=0; i < numresults; i++)
				vec.push_back( Value( min ) );
		} else {
			for (size_t i=0; i < numresults; i++) {
				if ( deterministic ) {
					vec.push_back( Value( distributor( deterministic_rng ) ) );
				} else {
					vec.push_back( Value( distributor( lessdeterministic_rng ) ) );
				}
			}
		}
		return Value(vec);
	}
quit:
	return Value();
}


Value builtin_min(const Context *, const EvalContext *evalctx)
{
	// preserve special handling of the first argument
	// as a template for vector processing
	size_t n = evalctx->numArgs();
	if (n >= 1) {
		const Value &v0 = evalctx->getArgValue(0);

		if (n == 1 && v0.type() == Value::VECTOR && !v0.toVector().empty()) {
			Value min = v0.toVector()[0];
			for (size_t i = 1; i < v0.toVector().size(); i++) {
				if (v0.toVector()[i] < min)
					min = v0.toVector()[i];
			}
			return min;
		}
		if (v0.type() == Value::NUMBER) {
			double val = v0.toDouble();
			for (size_t i = 1; i < n; ++i) {
				const Value &v = evalctx->getArgValue(i);
				// 4/20/14 semantic change per discussion:
				// break on any non-number
				if (v.type() != Value::NUMBER) goto quit;
				register double x = v.toDouble();
				if (x < val) val = x;
			}
			return Value(val);
		}
	}
quit:
	return Value();
}

Value builtin_max(const Context *, const EvalContext *evalctx)
{
	// preserve special handling of the first argument
	// as a template for vector processing
	size_t n = evalctx->numArgs();
	if (n >= 1) {
		const Value &v0 = evalctx->getArgValue(0);

		if (n == 1 && v0.type() == Value::VECTOR && !v0.toVector().empty()) {
			Value max = v0.toVector()[0];
			for (size_t i = 1; i < v0.toVector().size(); i++) {
				if (v0.toVector()[i] > max)
					max = v0.toVector()[i];
			}
			return max;
		}
		if (v0.type() == Value::NUMBER) {
			double val = v0.toDouble();
			for (size_t i = 1; i < n; ++i) {
				const Value &v = evalctx->getArgValue(i);
				// 4/20/14 semantic change per discussion:
				// break on any non-number
				if (v.type() != Value::NUMBER) goto quit;
				register double x = v.toDouble();
				if (x > val) val = x;
			}
			return Value(val);
		}
	}
quit:
	return Value();
}

// this limit assumes 26+26=52 bits mantissa
// comment/undefine it to disable domain check
#define TRIG_HUGE_VAL ((1L<<26)*360.0*(1L<<26))

Value builtin_sin(const Context *, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		const Value &v = evalctx->getArgValue(0);
		if (v.type() == Value::NUMBER) {
			register double x = v.toDouble();
			// use positive tests because of possible Inf/NaN
			if (x < 360.0 && x >= 0.0) {
				// Ok for now
			} else
#ifdef TRIG_HUGE_VAL
			if (x < TRIG_HUGE_VAL && x > -TRIG_HUGE_VAL)
#endif
			{
				register double revolutions = floor(x/360.0);
				x -= 360.0*revolutions;
			}
#ifdef TRIG_HUGE_VAL
			else {
				// total loss of computational accuracy
				// the result would be meaningless
				return Value(std::numeric_limits<double>::quiet_NaN());
			}
#endif
			bool oppose = x >= 180.0;
			if (oppose) x -= 180.0;
			if (x > 90.0) x = 180.0 - x;
			if (x < 45.0) {
				if (x == 30.0) x = 0.5;
				else x = sin(deg2rad(x));
			} else if (x == 45.0)
				x = M_SQRT1_2;
			else // Inf/Nan would fall here
				x = cos(deg2rad(90.0-x));

			return Value(oppose ? -x : x);
		}
	}
	return Value();
}

Value builtin_cos(const Context *, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		const Value &v = evalctx->getArgValue(0);
		if (v.type() == Value::NUMBER) {
			register double x = v.toDouble();
			// use positive tests because of possible Inf/NaN
			if (x < 360.0 && x >= 0.0) {
				// Ok for now
			} else
#ifdef TRIG_HUGE_VAL
			if (x < TRIG_HUGE_VAL && x > -TRIG_HUGE_VAL)
#endif
			{
				register double revolutions = floor(x/360.0);
				x -= 360.0*revolutions;
			}
#ifdef TRIG_HUGE_VAL
			else {
				// total loss of computational accuracy
				// the result would be meaningless
				return Value(std::numeric_limits<double>::quiet_NaN());
			}
#endif
			bool oppose = x >= 180.0;
			if (oppose) x -= 180.0;
			if (x > 90.0) {
				x = 180.0 - x;
				oppose = !oppose;
			}
			if (x > 45.0) {
				if (x == 60.0) x = 0.5;
				else x = sin(deg2rad(90.0-x));
			} else if (x == 45.0)
				x = M_SQRT1_2;
			else // Inf/Nan would fall here
				x = cos(deg2rad(x));

			return Value(oppose ? -x : x);
		}
	}
	return Value();
}

Value builtin_asin(const Context *, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		const Value &v = evalctx->getArgValue(0);
		if (v.type() == Value::NUMBER)
			return Value(rad2deg(asin(v.toDouble())));
	}
	return Value();
}

Value builtin_acos(const Context *, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		const Value &v = evalctx->getArgValue(0);
		if (v.type() == Value::NUMBER)
			return Value(rad2deg(acos(v.toDouble())));
	}
	return Value();
}

Value builtin_tan(const Context *, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		const Value &v = evalctx->getArgValue(0);
		if (v.type() == Value::NUMBER)
			return Value(tan(deg2rad(v.toDouble())));
	}
	return Value();
}

Value builtin_atan(const Context *, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		const Value &v = evalctx->getArgValue(0);
		if (v.type() == Value::NUMBER)
			return Value(rad2deg(atan(v.toDouble())));
	}
	return Value();
}

Value builtin_atan2(const Context *, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 2) {
		Value v0 = evalctx->getArgValue(0), v1 = evalctx->getArgValue(1);
		if (v0.type() == Value::NUMBER && v1.type() == Value::NUMBER)
			return Value(rad2deg(atan2(v0.toDouble(), v1.toDouble())));
	}
	return Value();
}

Value builtin_pow(const Context *, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 2) {
		Value v0 = evalctx->getArgValue(0), v1 = evalctx->getArgValue(1);
		if (v0.type() == Value::NUMBER && v1.type() == Value::NUMBER)
			return Value(pow(v0.toDouble(), v1.toDouble()));
	}
	return Value();
}

Value builtin_round(const Context *, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		const Value &v = evalctx->getArgValue(0);
		if (v.type() == Value::NUMBER)
			return Value(round(v.toDouble()));
	}
	return Value();
}

Value builtin_ceil(const Context *, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		const Value &v = evalctx->getArgValue(0);
		if (v.type() == Value::NUMBER)
			return Value(ceil(v.toDouble()));
	}
	return Value();
}

Value builtin_floor(const Context *, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		const Value &v = evalctx->getArgValue(0);
		if (v.type() == Value::NUMBER)
			return Value(floor(v.toDouble()));
	}
	return Value();
}

Value builtin_sqrt(const Context *, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		const Value &v = evalctx->getArgValue(0);
		if (v.type() == Value::NUMBER)
			return Value(sqrt(v.toDouble()));
	}
	return Value();
}

Value builtin_exp(const Context *, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		const Value &v = evalctx->getArgValue(0);
		if (v.type() == Value::NUMBER)
			return Value(exp(v.toDouble()));
	}
	return Value();
}

Value builtin_length(const Context *, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		const Value &v = evalctx->getArgValue(0);
		if (v.type() == Value::VECTOR) return Value(int(v.toVector().size()));
		if (v.type() == Value::STRING) {
			//Unicode glyph count for the length -- rather than the string (num. of bytes) length.
			std::string text = v.toString();
			return Value(int( g_utf8_strlen( text.c_str(), text.size() ) ));
		}
	}
	return Value();
}

Value builtin_log(const Context *, const EvalContext *evalctx)
{
	size_t n = evalctx->numArgs();
	if (n == 1 || n == 2) {
		const Value &v0 = evalctx->getArgValue(0);
		if (v0.type() == Value::NUMBER) {
			double x = 10.0, y = v0.toDouble();
			if (n > 1) {
				const Value &v1 = evalctx->getArgValue(1);
				if (v1.type() != Value::NUMBER) goto quit;
				x = y; y = v1.toDouble();
			}
			return Value(log(y) / log(x));
		}
	}
quit:
	return Value();
}

Value builtin_ln(const Context *, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		const Value &v = evalctx->getArgValue(0);
		if (v.type() == Value::NUMBER)
			return Value(log(v.toDouble()));
	}
	return Value();
}

Value builtin_str(const Context *, const EvalContext *evalctx)
{
	std::stringstream stream;

	for (size_t i = 0; i < evalctx->numArgs(); i++) {
		stream << evalctx->getArgValue(i).toString();
	}
	return Value(stream.str());
}

Value builtin_concat(const Context *, const EvalContext *evalctx)
{
	Value::VectorType result;

	for (size_t i = 0; i < evalctx->numArgs(); i++) {
		const Value v = evalctx->getArgValue(i);
		if (v.type() == Value::VECTOR) {
			Value::VectorType vec = v.toVector();
			for (Value::VectorType::const_iterator it = vec.begin(); it != vec.end(); it++) {
				result.push_back(*it);
			}
		} else {
			result.push_back(v);
		}
	}
	return Value(result);
}

Value builtin_lookup(const Context *, const EvalContext *evalctx)
{
	double p, low_p, low_v, high_p, high_v;
	if (evalctx->numArgs() < 2 ||                     // Needs two args
			!evalctx->getArgValue(0).getDouble(p) ||                  // First must be a number
			evalctx->getArgValue(1).toVector()[0].toVector().size() < 2) // Second must be a vector of vectors
		return Value();
	if (!evalctx->getArgValue(1).toVector()[0].getVec2(low_p, low_v) || !evalctx->getArgValue(1).toVector()[0].getVec2(high_p, high_v))
		return Value();
	for (size_t i = 1; i < evalctx->getArgValue(1).toVector().size(); i++) {
		double this_p, this_v;
		if (evalctx->getArgValue(1).toVector()[i].getVec2(this_p, this_v)) {
			if (this_p <= p && (this_p > low_p || low_p > p)) {
				low_p = this_p;
				low_v = this_v;
			}
			if (this_p >= p && (this_p < high_p || high_p < p)) {
				high_p = this_p;
				high_v = this_v;
			}
		}
	}
	if (p <= low_p)
		return Value(high_v);
	if (p >= high_p)
		return Value(low_v);
	double f = (p-low_p) / (high_p-low_p);
	return Value(high_v * f + low_v * (1-f));
}

/*
 Pattern:

  "search" "(" ( match_value | list_of_match_values ) "," vector_of_vectors
        ("," num_returns_per_match
          ("," index_col_num )? )?
        ")";
  match_value : ( Value::NUMBER | Value::STRING );
  list_of_values : "[" match_value ("," match_value)* "]";
  vector_of_vectors : "[" ("[" Value ("," Value)* "]")+ "]";
  num_returns_per_match : int;
  index_col_num : int;

 The search string and searched strings can be unicode strings.
 Examples:
  Index values return as list:
    search("a","abcdabcd");
        - returns [0]
    search("Л","Л");  //A unicode string
        - returns [0]
    search("🂡aЛ","a🂡Л🂡a🂡Л🂡a",0);
        - returns [[1,3,5,7],[0,4,8],[2,6]]
    search("a","abcdabcd",0); //Search up to all matches
        - returns [[0,4]]
    search("a","abcdabcd",1);
        - returns [0]
    search("e","abcdabcd",1);
        - returns []
    search("a",[ ["a",1],["b",2],["c",3],["d",4],["a",5],["b",6],["c",7],["d",8],["e",9] ]);
        - returns [0,4]

  Search on different column; return Index values:
    search(3,[ ["a",1],["b",2],["c",3],["d",4],["a",5],["b",6],["c",7],["d",8],["e",3] ], 0, 1);
        - returns [0,8]

  Search on list of values:
    Return all matches per search vector element:
      search("abc",[ ["a",1],["b",2],["c",3],["d",4],["a",5],["b",6],["c",7],["d",8],["e",9] ], 0);
        - returns [[0,4],[1,5],[2,6]]

    Return first match per search vector element; special case return vector:
      search("abc",[ ["a",1],["b",2],["c",3],["d",4],["a",5],["b",6],["c",7],["d",8],["e",9] ], 1);
        - returns [0,1,2]

    Return first two matches per search vector element; vector of vectors:
      search("abce",[ ["a",1],["b",2],["c",3],["d",4],["a",5],["b",6],["c",7],["d",8],["e",9] ], 2);
        - returns [[0,4],[1,5],[2,6],[8]]

*/
#pragma clang diagnostic ignored "-Wlogical-op-parentheses"


static Value::VectorType search(const std::string &find, const std::string &table,
																unsigned int num_returns_per_match, unsigned int index_col_num)
{
	Value::VectorType returnvec;
	//Unicode glyph count for the length
	size_t findThisSize = g_utf8_strlen(find.c_str(), find.size());
	size_t searchTableSize = g_utf8_strlen(table.c_str(), table.size());
	for (size_t i = 0; i < findThisSize; i++) {
		unsigned int matchCount = 0;
		Value::VectorType resultvec;
		for (size_t j = 0; j < searchTableSize; j++) {
			const gchar *ptr_ft = g_utf8_offset_to_pointer(find.c_str(), i);
			const gchar *ptr_st = g_utf8_offset_to_pointer(table.c_str(), j);
			if (ptr_ft && ptr_st && (g_utf8_get_char(ptr_ft) == g_utf8_get_char(ptr_st)) ) {
				matchCount++;
				if (num_returns_per_match == 1) {
					returnvec.push_back(Value(double(j)));
					break;
				} else {
					resultvec.push_back(Value(double(j)));
				}
				if (num_returns_per_match > 1 && matchCount >= num_returns_per_match) {
					break;
				}
			}
		}
		if (matchCount == 0) {
			gchar *ptr_ft = g_utf8_offset_to_pointer(find.c_str(), i);
			gchar utf8_of_cp[6] = ""; //A buffer for a single unicode character to be copied into
			if (ptr_ft) g_utf8_strncpy(utf8_of_cp, ptr_ft, 1);
			PRINTB("  WARNING: search term not found: \"%s\"", utf8_of_cp);
		}
		if (num_returns_per_match == 0 || num_returns_per_match > 1) {
			returnvec.push_back(Value(resultvec));
		}
	}
	return returnvec;
}

static Value::VectorType search(const std::string &find, const Value::VectorType &table,
																unsigned int num_returns_per_match, unsigned int index_col_num)
{
	Value::VectorType returnvec;
	//Unicode glyph count for the length
	unsigned int findThisSize =  g_utf8_strlen(find.c_str(), find.size());
	unsigned int searchTableSize = table.size();
	for (size_t i = 0; i < findThisSize; i++) {
		unsigned int matchCount = 0;
		Value::VectorType resultvec;
		for (size_t j = 0; j < searchTableSize; j++) {
			const gchar *ptr_ft = g_utf8_offset_to_pointer(find.c_str(), i);
			const gchar *ptr_st = g_utf8_offset_to_pointer(table[j].toVector()[index_col_num].toString().c_str(), 0);
			if (ptr_ft && ptr_st && (g_utf8_get_char(ptr_ft) == g_utf8_get_char(ptr_st)) ) {
				matchCount++;
				if (num_returns_per_match == 1) {
					returnvec.push_back(Value(double(j)));
					break;
				} else {
					resultvec.push_back(Value(double(j)));
				}
				if (num_returns_per_match > 1 && matchCount >= num_returns_per_match) {
					break;
				}
			}
		}
		if (matchCount == 0) {
			const gchar *ptr_ft = g_utf8_offset_to_pointer(find.c_str(), i);
			gchar utf8_of_cp[6] = ""; //A buffer for a single unicode character to be copied into
			if (ptr_ft) g_utf8_strncpy(utf8_of_cp, ptr_ft, 1);
			PRINTB("  WARNING: search term not found: \"%s\"", utf8_of_cp);
		}
		if (num_returns_per_match == 0 || num_returns_per_match > 1) {
			returnvec.push_back(Value(resultvec));
		}
	}
	return returnvec;
}

Value builtin_search(const Context *, const EvalContext *evalctx)
{
	if (evalctx->numArgs() < 2) return Value();

	const Value &findThis = evalctx->getArgValue(0);
	const Value &searchTable = evalctx->getArgValue(1);
	unsigned int num_returns_per_match = (evalctx->numArgs() > 2) ? evalctx->getArgValue(2).toDouble() : 1;
	unsigned int index_col_num = (evalctx->numArgs() > 3) ? evalctx->getArgValue(3).toDouble() : 0;

	Value::VectorType returnvec;

	if (findThis.type() == Value::NUMBER) {
		unsigned int matchCount = 0;

		for (size_t j = 0; j < searchTable.toVector().size(); j++) {
			const Value& search_element = searchTable.toVector()[j];

			if (index_col_num == 0 && findThis == search_element ||
					index_col_num  < search_element.toVector().size() &&
					findThis      == search_element.toVector()[index_col_num]) {
				returnvec.push_back(Value(double(j)));
				matchCount++;
				if (num_returns_per_match != 0 && matchCount >= num_returns_per_match) break;
			}
		}
	} else if (findThis.type() == Value::STRING) {
		if (searchTable.type() == Value::STRING) {
			returnvec = search(findThis.toString(), searchTable.toString(), num_returns_per_match, index_col_num);
		}
		else {
			returnvec = search(findThis.toString(), searchTable.toVector(), num_returns_per_match, index_col_num);
		}
	} else if (findThis.type() == Value::VECTOR) {
		for (size_t i = 0; i < findThis.toVector().size(); i++) {
		  unsigned int matchCount = 0;
			Value::VectorType resultvec;

			Value const& find_value = findThis.toVector()[i];

			for (size_t j = 0; j < searchTable.toVector().size(); j++) {

				Value const& search_element = searchTable.toVector()[j];

				if (index_col_num == 0 && find_value == search_element
					||
					index_col_num  < search_element.toVector().size() &&
					find_value    == search_element.toVector()[index_col_num])
				{
					Value resultValue((double(j)));
		      matchCount++;
		      if (num_returns_per_match == 1) {
						returnvec.push_back(resultValue);
						break;
		      } else {
						resultvec.push_back(resultValue);
		      }
		      if (num_returns_per_match > 1 && matchCount >= num_returns_per_match) break;
		    }
		  }
		  if (num_returns_per_match == 1 && matchCount == 0) {
		    if (findThis.toVector()[i].type() == Value::NUMBER) {
					PRINTB("  WARNING: search term not found: %s",findThis.toVector()[i].toDouble());
				}
		    else if (findThis.toVector()[i].type() == Value::STRING) {
					PRINTB("  WARNING: search term not found: \"%s\"",findThis.toVector()[i].toString());
				}
		    returnvec.push_back(Value(resultvec));
		  }
		  if (num_returns_per_match == 0 || num_returns_per_match > 1) {
				returnvec.push_back(Value(resultvec));
			}
		}
	} else {
		PRINTB("  WARNING: search: none performed on input %s", findThis);
		return Value();
	}
	return Value(returnvec);
}

#define QUOTE(x__) # x__
#define QUOTED(x__) QUOTE(x__)

Value builtin_version(const Context *, const EvalContext *evalctx)
{
	(void)evalctx; // unusued parameter
	Value::VectorType val;
	val.push_back(Value(double(OPENSCAD_YEAR)));
	val.push_back(Value(double(OPENSCAD_MONTH)));
#ifdef OPENSCAD_DAY
	val.push_back(Value(double(OPENSCAD_DAY)));
#endif
	return Value(val);
}

Value builtin_version_num(const Context *ctx, const EvalContext *evalctx)
{
	Value val = (evalctx->numArgs() == 0) ? builtin_version(ctx, evalctx) : evalctx->getArgValue(0);
	double y, m, d = 0;
	if (!val.getVec3(y, m, d)) {
		if (!val.getVec2(y, m)) {
			return Value();
		}
	}
	return Value(y * 10000 + m * 100 + d);
}

Value builtin_parent_module(const Context *, const EvalContext *evalctx)
{
	int n;
	double d;
	int s = Module::stack_size();
	if (evalctx->numArgs() == 0)
		d=1; // parent module
	else if (evalctx->numArgs() == 1) {
		const Value &v = evalctx->getArgValue(0);
		if (v.type() != Value::NUMBER) return Value();
		v.getDouble(d);
	} else
			return Value();
	n=trunc(d);
	if (n < 0) {
		PRINTB("WARNING: Negative parent module index (%d) not allowed", n);
		return Value();
	}
	if (n >= s) {
		PRINTB("WARNING: Parent module index (%d) greater than the number of modules on the stack", n);
		return Value();
	}
	return Value(Module::stack_element(s - 1 - n));
}

Value builtin_norm(const Context *, const EvalContext *evalctx)
{
	if (evalctx->numArgs() == 1) {
		const Value &val = evalctx->getArgValue(0);
		if (val.type() == Value::VECTOR) {
			double sum = 0;
			Value::VectorType v = val.toVector();
			size_t n = v.size();
			for (size_t i = 0; i < n; i++)
				if (v[i].type() == Value::NUMBER) {
					// sum += pow(v[i].toDouble(),2);
					register double x = v[i].toDouble();
					sum += x*x;
				} else {
					PRINT("  WARNING: Incorrect arguments to norm()");
					return Value();
				}
			return Value(sqrt(sum));
		}
	}
	return Value();
}

Value builtin_cross(const Context *, const EvalContext *evalctx)
{
	if (evalctx->numArgs() != 2) {
		PRINT("WARNING: Invalid number of parameters for cross()");
		return Value();
	}
	
	Value arg0 = evalctx->getArgValue(0);
	Value arg1 = evalctx->getArgValue(1);
	if ((arg0.type() != Value::VECTOR) || (arg1.type() != Value::VECTOR)) {
		PRINT("WARNING: Invalid type of parameters for cross()");
		return Value();
	}
	
	Value::VectorType v0 = arg0.toVector();
	Value::VectorType v1 = arg1.toVector();
	if ((v0.size() != 3) || (v1.size() != 3)) {
		PRINT("WARNING: Invalid vector size of parameter for cross()");
		return Value();
	}
	for (unsigned int a = 0;a < 3;a++) {
		if ((v0[a].type() != Value::NUMBER) || (v1[a].type() != Value::NUMBER)) {
			PRINT("WARNING: Invalid value in parameter vector for cross()");
			return Value();
		}
		double d0 = v0[a].toDouble();
		double d1 = v1[a].toDouble();
		if (boost::math::isnan(d0) || boost::math::isnan(d1)) {
			PRINT("WARNING: Invalid value (NaN) in parameter vector for cross()");
			return Value();
		}
		if (boost::math::isinf(d0) || boost::math::isinf(d1)) {
			PRINT("WARNING: Invalid value (INF) in parameter vector for cross()");
			return Value();
		}
	}
	
	double x = v0[1].toDouble() * v1[2].toDouble() - v0[2].toDouble() * v1[1].toDouble();
	double y = v0[2].toDouble() * v1[0].toDouble() - v0[0].toDouble() * v1[2].toDouble();
	double z = v0[0].toDouble() * v1[1].toDouble() - v0[1].toDouble() * v1[0].toDouble();
	
	Value::VectorType result;
	result.push_back(Value(x));
	result.push_back(Value(y));
	result.push_back(Value(z));
	return Value(result);
}

void register_builtin_functions()
{
	Builtins::init("abs", new BuiltinFunction(&builtin_abs));
	Builtins::init("sign", new BuiltinFunction(&builtin_sign));
	Builtins::init("rands", new BuiltinFunction(&builtin_rands));
	Builtins::init("min", new BuiltinFunction(&builtin_min));
	Builtins::init("max", new BuiltinFunction(&builtin_max));
	Builtins::init("sin", new BuiltinFunction(&builtin_sin));
	Builtins::init("cos", new BuiltinFunction(&builtin_cos));
	Builtins::init("asin", new BuiltinFunction(&builtin_asin));
	Builtins::init("acos", new BuiltinFunction(&builtin_acos));
	Builtins::init("tan", new BuiltinFunction(&builtin_tan));
	Builtins::init("atan", new BuiltinFunction(&builtin_atan));
	Builtins::init("atan2", new BuiltinFunction(&builtin_atan2));
	Builtins::init("round", new BuiltinFunction(&builtin_round));
	Builtins::init("ceil", new BuiltinFunction(&builtin_ceil));
	Builtins::init("floor", new BuiltinFunction(&builtin_floor));
	Builtins::init("pow", new BuiltinFunction(&builtin_pow));
	Builtins::init("sqrt", new BuiltinFunction(&builtin_sqrt));
	Builtins::init("exp", new BuiltinFunction(&builtin_exp));
	Builtins::init("len", new BuiltinFunction(&builtin_length));
	Builtins::init("log", new BuiltinFunction(&builtin_log));
	Builtins::init("ln", new BuiltinFunction(&builtin_ln));
	Builtins::init("str", new BuiltinFunction(&builtin_str));
	Builtins::init("concat", new BuiltinFunction(&builtin_concat, Feature::ExperimentalConcatFunction));
	Builtins::init("lookup", new BuiltinFunction(&builtin_lookup));
	Builtins::init("search", new BuiltinFunction(&builtin_search));
	Builtins::init("version", new BuiltinFunction(&builtin_version));
	Builtins::init("version_num", new BuiltinFunction(&builtin_version_num));
	Builtins::init("norm", new BuiltinFunction(&builtin_norm));
	Builtins::init("cross", new BuiltinFunction(&builtin_cross));
	Builtins::init("parent_module", new BuiltinFunction(&builtin_parent_module));
}
