/*
PAMHD collection of time-dependent boundaries setting variables from external file.

Copyright 2014, 2015 Ilja Honkonen
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice, this
  list of conditions and the following disclaimer in the documentation and/or
  other materials provided with the distribution.

* Neither the names of the copyright holders nor the names of their contributors
  may be used to endorse or promote products derived from this software
  without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/


#ifndef PAMHD_BOUNDARIES_VALUE_BOUNDARY_HPP
#define PAMHD_BOUNDARIES_VALUE_BOUNDARY_HPP


#include "cstdlib"
#include "iostream"
#include "string"
#include "utility"

#include "boost/optional.hpp"
#include "boost/program_options.hpp"
#include "prettyprint.hpp"

#include "boundaries/boundary_time_dependent.hpp"
#include "boundaries/boxes.hpp"
#include "boundaries/spheres.hpp"


namespace pamhd {
namespace boundaries {


template<
	class Cell_T,
	class Time_T,
	class Scalar_T,
	class Vector_T,
	class... Variables
> class Value_Boundaries
{
public:

	std::vector<
		Boundary_Time_Dependent<
			Boxes<Vector_T>,
			Cell_T,
			Time_T,
			Variables...
		>
	> boxes;

	std::vector<
		Boundary_Time_Dependent<
			Spheres<Vector_T, Scalar_T>,
			Cell_T,
			Time_T,
			Variables...
		>
	> spheres;



	/*!
	Adds options for setting the number of boundaries.

	Use to query the number of boundaries to create whose options
	can then be added with add_options().

	Must not be called after add_options().
	*/
	void add_initialization_options(
		const std::string& option_name_prefix,
		boost::program_options::options_description& options
	) {
		options.add_options()
			((option_name_prefix + "nr-boxes").c_str(),
				boost::program_options::value<size_t>(&this->number_of_boxes)
					->default_value(this->number_of_boxes),
				"Number of boxes in value boundary condition")
			((option_name_prefix + "nr-spheres").c_str(),
				boost::program_options::value<size_t>(&this->number_of_spheres)
					->default_value(this->number_of_spheres),
				"Number of spheres in value boundary condition");
	}


	/*!
	Add options for as many boundaries as given by the nr-*
	variables set by add_initialization_options.
	*/
	void add_options(
		const std::string& option_name_prefix,
		boost::program_options::options_description& options
	) {
		this->boxes.resize(this->number_of_boxes);
		this->spheres.resize(this->number_of_spheres);

		size_t counter;

		counter = 0;
		for (auto& box: this->boxes) {
			counter++;
			box.add_options(
				option_name_prefix
					+ "box"
					+ boost::lexical_cast<std::string>(counter)
					+ ".",
				options
			);
		}

		counter = 0;
		for (auto& sphere: this->spheres) {
			counter++;
			sphere.add_options(
				option_name_prefix
					+ "sphere"
					+ boost::lexical_cast<std::string>(counter)
					+ ".",
				options
			);
		}
	}


	/*!
	geometry_parameters is given to the overlaps function of each geometry
	object, see their documentation for the required parameters.

	Returns number of boundaries at given time to which given cell was added.

	Use clear_cells() to remove cells from all boundaries, for example, if
	(some) boundaries have moved and previous classifications aren't correct.
	*/
	template<class... Geometry_Parameters> size_t add_cell(
		const Time_T& time,
		const Cell_T& cell,
		Geometry_Parameters&&... geometry_parameters
	) {
		size_t ret_val = 0;

		for (auto& box: this->boxes) {
			if (
				box.add_cell(
					time,
					cell,
					std::forward<Geometry_Parameters>(geometry_parameters)...
				)
			) {
				ret_val++;
			}
		}

		for (auto& sphere: this->spheres) {
			if (
				sphere.add_cell(
					time,
					cell,
					std::forward<Geometry_Parameters>(geometry_parameters)...
				)
			) {
				ret_val++;
			}
		}

		return ret_val;
	}


	//! Returns number of existing boundaries
	size_t get_number_of_boundaries() const
	{
		return this->boxes.size() + this->spheres.size();
	}


	/*!
	Returns cells which belong to given boundary.

	Boundary indices are in the range [0, get_number_of_boundaries()[.
	*/
	const std::vector<Cell_T>& get_cells(
		size_t boundary_index
	) const {
		if (boundary_index < this->boxes.size()) {
			return this->boxes[boundary_index].get_cells();
		}
		boundary_index -= this->boxes.size();

		if (boundary_index < this->spheres.size()) {
			return this->spheres[boundary_index].get_cells();
		}

		std::cerr <<  __FILE__ << "(" << __LINE__<< ") "
			<< "Too large boundary index given: " << boundary_index
			<< ", should be at most: "
			<< this->boxes.size() + this->spheres.size() - 1
			<< std::endl;
		abort();
	}


	void clear_cells()
	{
		for (auto& box: this->boxes) {
			box.clear_cells();
		}

		for (auto& sphere: this->spheres) {
			sphere.clear_cells();
		}
	}


	void clear_boundaries()
	{
		this->boxes.clear();
		this->spheres.clear();
	}


	//! Returns data of given variable in given boundary at given time & position
	template<class Variable> typename Variable::data_type get_data(
		const Variable& variable,
		size_t boundary_index,
		const std::array<double, 3>& given_position,
		const Time_T& given_time
	) {
		if (boundary_index < this->boxes.size()) {
			return this->boxes[boundary_index].get_data(
				variable,
				given_position,
				given_time
			);
		}
		boundary_index -= this->boxes.size();

		if (boundary_index < this->spheres.size()) {
			return this->spheres[boundary_index].get_data(
				variable,
				given_position,
				given_time
			);
		}

		std::cerr <<  __FILE__ << "(" << __LINE__<< ") "
			<< "Too large boundary index given: "
			<< this->boxes.size() + boundary_index
			<< ", should be at most: "
			<< this->boxes.size() + this->spheres.size() - 1
			<< std::endl;
		abort();
	}


	/*!
	Invalidates previous boundary ids.
	Returns boundary index of added boundary if successfull.

	See functions in boundary_time_dependent.hpp on how to set
	geometry and variable data of added boundary.
	*/
	boost::optional<size_t> add_box()
	{
		this->boxes.push_back(
			Boundary_Time_Dependent<Boxes<Vector_T>, Cell_T, Time_T, Variables...>()
		);
		this->number_of_boxes = this->boxes.size();

		return boost::optional<size_t>(this->number_of_boxes - 1);
	}


	/*!
	Same as add_box() but adds a sphere boundary.
	*/
	boost::optional<size_t> add_sphere()
	{
		this->spheres.push_back(
			Boundary_Time_Dependent<Spheres<Vector_T, Scalar_T>, Cell_T, Time_T, Variables...>()
		);
		this->number_of_spheres = this->spheres.size();

		return boost::optional<size_t>(
			this->number_of_boxes + this->number_of_spheres - 1
		);
	}



private:

	size_t
		number_of_boxes = 0,
		number_of_spheres = 0;
};

}} // namespaces

#endif // ifndef PAMHD_BOUNDARIES_VALUE_BOUNDARY_HPP
