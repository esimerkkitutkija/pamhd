/*
Two-fluid reference test program for MHD solvers of PAMHD.

Copyright 2014, 2015 Ilja Honkonen
All rights reserved.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>.
*/


#include "array"
#include "boost/lexical_cast.hpp"
#include "boost/program_options.hpp"
#include "cmath"
#include "cstdlib"
#include "iostream"
#include "string"
#include "fstream"
#include "iomanip"
#include "iostream"
#include "limits"
#include "sstream"
#include "string"
#include "type_traits"

#include "gensimcell.hpp"

#include "mhd/common.hpp"
#include "mhd/hll_athena.hpp"
#include "mhd/hlld_athena.hpp"
#include "mhd/roe_athena.hpp"
#include "mhd/variables.hpp"


using namespace std;


constexpr double
	adiabatic_index = 5.0 / 3.0,    
	vacuum_permeability = 4e-7 * M_PI,
	proton_mass = 1.672621777e-27;


// 1 boundary cell in each direction
constexpr size_t grid_length = 1000 + 2;

// only one magnetic field needed
using HD_Conservative = gensimcell::Cell<
	gensimcell::Always_Transfer,
	pamhd::mhd::Mass_Density,
	pamhd::mhd::Momentum_Density,
	pamhd::mhd::Total_Energy_Density
>;

struct HD1 { using data_type = HD_Conservative; };
struct HD2 { using data_type = HD_Conservative; };

using Cell = gensimcell::Cell<
	gensimcell::Never_Transfer,
	pamhd::mhd::MHD_State_Conservative, // total, given to solver
	HD1,
	HD2,
	pamhd::mhd::MHD_Flux_Conservative
>;
using Grid = std::array<Cell, grid_length>;

/*
Accessors to simulation variables.

Return a reference to data of corresponding variable in given simulation cell.
*/

// conservative MHD variables
const auto Mas
	= [](Cell& cell_data)->typename pamhd::mhd::Mass_Density::data_type&{
		return cell_data[pamhd::mhd::MHD_State_Conservative()][pamhd::mhd::Mass_Density()];
	};

const auto Mom
	= [](Cell& cell_data)->typename pamhd::mhd::Momentum_Density::data_type&{
		return cell_data[pamhd::mhd::MHD_State_Conservative()][pamhd::mhd::Momentum_Density()];
	};

const auto Nrj
	= [](Cell& cell_data)->typename pamhd::mhd::Total_Energy_Density::data_type&{
		return cell_data[pamhd::mhd::MHD_State_Conservative()][pamhd::mhd::Total_Energy_Density()];
	};

const auto Mag
	= [](Cell& cell_data)->typename pamhd::mhd::Magnetic_Field::data_type&{
		return cell_data[pamhd::mhd::MHD_State_Conservative()][pamhd::mhd::Magnetic_Field()];
	};

// fluxes of conservative MHD variables
const auto Mas_f
	= [](Cell& cell_data)->typename pamhd::mhd::Mass_Density::data_type&{
		return cell_data[pamhd::mhd::MHD_Flux_Conservative()][pamhd::mhd::Mass_Density()];
	};

const auto Mom_f
	= [](Cell& cell_data)->typename pamhd::mhd::Momentum_Density::data_type&{
		return cell_data[pamhd::mhd::MHD_Flux_Conservative()][pamhd::mhd::Momentum_Density()];
	};

const auto Nrj_f
	= [](Cell& cell_data)->typename pamhd::mhd::Total_Energy_Density::data_type&{
		return cell_data[pamhd::mhd::MHD_Flux_Conservative()][pamhd::mhd::Total_Energy_Density()];
	};

const auto Mag_f
	= [](Cell& cell_data)->typename pamhd::mhd::Magnetic_Field::data_type&{
		return cell_data[pamhd::mhd::MHD_Flux_Conservative()][pamhd::mhd::Magnetic_Field()];
	};

// fluid variables
const auto Mas1
	= [](Cell& cell_data)->typename pamhd::mhd::Mass_Density::data_type&{
		return cell_data[HD1()][pamhd::mhd::Mass_Density()];
	};
const auto Mom1
	= [](Cell& cell_data)->typename pamhd::mhd::Momentum_Density::data_type&{
		return cell_data[HD1()][pamhd::mhd::Momentum_Density()];
	};
const auto Nrj1
	= [](Cell& cell_data)->typename pamhd::mhd::Total_Energy_Density::data_type&{
		return cell_data[HD1()][pamhd::mhd::Total_Energy_Density()];
	};
const auto Mas2
	= [](Cell& cell_data)->typename pamhd::mhd::Mass_Density::data_type&{
		return cell_data[HD2()][pamhd::mhd::Mass_Density()];
	};
const auto Mom2
	= [](Cell& cell_data)->typename pamhd::mhd::Momentum_Density::data_type&{
		return cell_data[HD2()][pamhd::mhd::Momentum_Density()];
	};
const auto Nrj2
	= [](Cell& cell_data)->typename pamhd::mhd::Total_Energy_Density::data_type&{
		return cell_data[HD2()][pamhd::mhd::Total_Energy_Density()];
	};


/*!
Returns start coordinate of the grid.
*/
constexpr double get_grid_start()
{
	return 0.0;
}

/*!
Returns end coordinate of the grid.
*/
constexpr double get_grid_end()
{
	return 1e5;
}


/*!
Returns the size of a grid cell.
*/
constexpr double get_cell_size()
{
	static_assert(
		std::tuple_size<Grid>::value > 0,
		"Grid must have at least one cell"
	);
	return (get_grid_end() - get_grid_start()) / std::tuple_size<Grid>::value;
}


/*!
Returns the center of a cell located at given index in given grid.

Index starts from 0.
Returns a quiet NaN in case of error.
*/
double get_cell_center(const size_t index)
{
	static_assert(
		std::tuple_size<Grid>::value > 0,
		"Grid must have at least one cell"
	);

	if (index >= std::tuple_size<Grid>::value) {
		return std::numeric_limits<double>::quiet_NaN();
	}

	return (0.5 + index) * get_cell_size();
}


//! Sets the initial state of simulation, zeroes fluxes
void initialize_mhd(
	Grid& grid,
	const double adiabatic_index,
	const double vacuum_permeability
) {
	static_assert(
		std::tuple_size<Grid>::value > 0,
		"Grid must have at least one cell"
	);

	const pamhd::mhd::MHD_State_Conservative MHD{};
	const pamhd::mhd::MHD_Flux_Conservative MHDF{};
	const pamhd::mhd::Mass_Density Mas{};
	const pamhd::mhd::Momentum_Density Mom{};
	const pamhd::mhd::Total_Energy_Density Nrj{};
	const pamhd::mhd::Magnetic_Field Mag{};

	for (size_t cell_i = 0; cell_i < grid.size(); cell_i++) {
		auto& cell_data = grid[cell_i];

		cell_data[MHD][Mas]   =
		cell_data[HD1()][Mas] =
		cell_data[HD2()][Mas] =
		cell_data[MHDF][Mas]  =

		cell_data[MHD][Mom][0]   =
		cell_data[HD1()][Mom][0] =
		cell_data[HD2()][Mom][0] =
		cell_data[MHDF][Mom][0]  =
		cell_data[MHD][Mom][1]   =
		cell_data[HD1()][Mom][1] =
		cell_data[HD2()][Mom][1] =
		cell_data[MHDF][Mom][1]  =
		cell_data[MHD][Mom][2]   =
		cell_data[HD1()][Mom][2] =
		cell_data[HD2()][Mom][2] =
		cell_data[MHDF][Mom][2]  =

		cell_data[MHD][Nrj]   =
		cell_data[HD1()][Nrj] =
		cell_data[HD2()][Nrj] =
		cell_data[MHDF][Nrj]  =

		cell_data[MHD][Mag][0]  =
		cell_data[MHDF][Mag][0] =
		cell_data[MHD][Mag][1]  =
		cell_data[MHDF][Mag][1] =
		cell_data[MHD][Mag][2]  =
		cell_data[MHDF][Mag][2] = 0;

		cell_data[MHD][Mag][0] = 1.5e-9;

		// in every cell all mass must be in one of the fluids
		const double center = get_cell_center(cell_i);
		if (center < (get_grid_end() - get_grid_start()) / 2) {
			cell_data[HD1()][Mas] = proton_mass * 3e6;
			cell_data[MHD][Mag][1] = 1e-9;
			cell_data[HD1()][Nrj] = pamhd::mhd::get_total_energy_density(
				cell_data[HD1()][Mas],
				cell_data[HD1()][Mom],
				3e-12,
				cell_data[MHD][Mag],
				adiabatic_index,
				vacuum_permeability
			);
		} else {
			cell_data[HD2()][Mas] = proton_mass * 1e6;
			cell_data[MHD][Mag][1] = -1e-9;
			cell_data[HD2()][Nrj] = pamhd::mhd::get_total_energy_density(
				cell_data[HD2()][Mas],
				cell_data[HD2()][Mom],
				1e-12,
				cell_data[MHD][Mag],
				adiabatic_index,
				vacuum_permeability
			);
		}
	}
}

/*!
Advances the MHD solution for one time step of length dt with given solver.

Returns the maximum allowed length of time step for the
next step.
*/
template <
	class Solver,
	class Mass_Density_Getter,
	class Momentum_Density_Getter,
	class Total_Energy_Density_Getter,
	class Magnetic_Field_Getter,
	class Mass_Density_Flux_Getter,
	class Momentum_Density_Flux_Getter,
	class Total_Energy_Density_Flux_Getter,
	class Magnetic_Field_Flux_Getter
> double solve_mhd(
	const Solver solver,
	Grid& grid,
	const double dt,
	const double adiabatic_index,
	const double vacuum_permeability,
	const Mass_Density_Getter Mas,
	const Momentum_Density_Getter Mom,
	const Total_Energy_Density_Getter Nrj,
	const Magnetic_Field_Getter Mag,
	const Mass_Density_Flux_Getter Mas_f,
	const Momentum_Density_Flux_Getter Mom_f,
	const Total_Energy_Density_Flux_Getter Nrj_f,
	const Magnetic_Field_Flux_Getter Mag_f
) {
	static_assert(
		std::tuple_size<Grid>::value >= 3,
		"Grid must have at least three cells"
	);

	// shorthand notation for variable used internally
	const pamhd::mhd::Mass_Density mas{};
	const pamhd::mhd::Momentum_Density mom{};
	const pamhd::mhd::Total_Energy_Density nrj{};
	const pamhd::mhd::Magnetic_Field mag{};

	const double
		cell_size = get_cell_size(),
		face_area = 1.0;

	double max_dt = std::numeric_limits<double>::max();

	// calculate fluxes
	for (size_t cell_i = 0; cell_i < std::tuple_size<Grid>::value - 1; cell_i++) {

		Cell
			&cell = grid[cell_i],
			&neighbor = grid[cell_i + 1];

		double max_vel = -1;
		pamhd::mhd::MHD_Conservative state_neg, state_pos, flux_neg, flux_pos;
		state_neg[mas] = Mas(cell);
		state_neg[mom] = Mom(cell);
		state_neg[nrj] = Nrj(cell);
		state_neg[mag] = Mag(cell);
		state_pos[mas] = Mas(neighbor);
		state_pos[mom] = Mom(neighbor);
		state_pos[nrj] = Nrj(neighbor);
		state_pos[mag] = Mag(neighbor);

		std::tie(
			flux_neg,
			flux_pos,
			max_vel
		) = solver(
			state_neg,
			state_pos,
			face_area,
			dt,
			adiabatic_index,
			vacuum_permeability
		);

		max_dt = std::min(max_dt, cell_size / max_vel);

		// assumes all mass is in one of the fluids in every cell
		if (cell_i > 0) {
			// positive flux flows neg->pos, i.e. out of current cell
			Mas_f(cell) -= flux_neg[mas] + flux_pos[mas];
			Mom_f(cell) -= flux_neg[mom] + flux_pos[mom];
			Nrj_f(cell) -= flux_neg[nrj] + flux_pos[nrj];
			Mag_f(cell) -= flux_neg[mag] + flux_pos[mag];
		}
		if (cell_i < std::tuple_size<Grid>::value - 2) {
			Mas_f(neighbor) += flux_neg[mas] + flux_pos[mas];
			Mom_f(neighbor) += flux_neg[mom] + flux_pos[mom];
			Nrj_f(neighbor) += flux_neg[nrj] + flux_pos[nrj];
			Mag_f(neighbor) += flux_neg[mag] + flux_pos[mag];
		}
	}

	return max_dt;
}


/*!
Writes given advection simulation to a file plottable with gnuplot.

Returns the name of the file written which
is derived from given simulation time.
*/
template <
	class Mass_Density_Getter,
	class Momentum_Density_Getter,
	class Total_Energy_Density_Getter,
	class Magnetic_Field_Getter
> std::string plot_mhd(
	const std::string& file_name_prefix,
	Grid& grid,
	const double simulation_time,
	const double adiabatic_index,
	const double vacuum_permeability,
	const Mass_Density_Getter Mas,
	const Momentum_Density_Getter Mom,
	const Total_Energy_Density_Getter Nrj,
	const Magnetic_Field_Getter Mag
) {
	std::ostringstream time_string, normalization_string;
	time_string
		<< std::setw(4)
		<< std::setfill('0')
		<< size_t(simulation_time * 1000);

	const std::string
		gnuplot_file_name(
			file_name_prefix
			+ time_string.str()
			+ "_ms.dat"
		),
		plot_file_name(
			file_name_prefix
			+ time_string.str()
			+ "_ms.png"
		),
		B_plot_file_name(
			file_name_prefix + "B_"
			+ time_string.str()
			+ "_ms.png"
		),
		v_plot_file_name(
			file_name_prefix + "v_"
			+ time_string.str()
			+ "_ms.png"
		);

	std::ofstream gnuplot_file(gnuplot_file_name);

	gnuplot_file
		<< "set term png enhanced\nset output '"
		<< plot_file_name
		<< "'\nset xlabel 'position'\n"
		   "set format y '%1.2e'\n"
		   "set format y2 '%1.2e'\n"
		   "set ylabel 'density/pressure'\n"
		   "set ytics nomirror\n"
		   "set y2tics nomirror\n"
		   "set key horizontal outside bottom\n"
		   "plot "
		     "'-' using 1:2 with line linewidth 2 title 'density', "
		     "'-' u 1:2 axis x1y2 w l lw 2 t 'pressure'\n"
		     ;

	// mass density
	for (size_t cell_i = 0; cell_i < std::tuple_size<Grid>::value; cell_i++) {
		auto& cell_data = grid[cell_i];
		const double x
			= get_cell_center(cell_i)
			/ (get_grid_end() - get_grid_start());
		gnuplot_file << x << " " << Mas(cell_data) << "\n";
	}
	gnuplot_file << "end\n";
	// pressure
	for (size_t cell_i = 0; cell_i < std::tuple_size<Grid>::value; cell_i++) {
		auto& cell_data = grid[cell_i];
		const double x = get_cell_center(cell_i) / (get_grid_end() - get_grid_start());
		double pressure = 0;
		if (Mas(cell_data) > 0) {
			pressure = pamhd::mhd::get_pressure(
				Mas(cell_data),
				Mom(cell_data),
				Nrj(cell_data),
				Mag(cell_data),
				adiabatic_index,
				vacuum_permeability
			);
		}
		gnuplot_file << x << " " << pressure << "\n";
	}
	gnuplot_file << "end\n";

	gnuplot_file
		<< "set term png enhanced\nset output '"
		<< v_plot_file_name
		<< "'\nset xlabel 'position'\n"
		   "set ylabel 'velocity'\n"
		   "unset y2tics\n"
		   "set key horizontal outside bottom\n"
		   "plot "
		     "'-' u 1:2 w l lw 2 t 'v_x', "
		     "'-' u 1:2 w l lw 2 t 'v_y', "
		     "'-' u 1:2 w l lw 2 t 'v_z'\n"
		;
	// vx
	for (size_t cell_i = 0; cell_i < std::tuple_size<Grid>::value; cell_i++) {
		auto& cell_data = grid[cell_i];
		double vx = 0;
		if (Mas(cell_data) > 0) {
			vx = Mom(cell_data)[0] / Mas(cell_data);
		}
		const double x = get_cell_center(cell_i) / (get_grid_end() - get_grid_start());
		gnuplot_file << x << " " << vx << "\n";
	}
	gnuplot_file << "end\n";
	// vy
	for (size_t cell_i = 0; cell_i < std::tuple_size<Grid>::value; cell_i++) {
		auto& cell_data = grid[cell_i];
		double vy = 0;
		if (Mas(cell_data) > 0) {
			vy = Mom(cell_data)[1] / Mas(cell_data);
		}
		const double x = get_cell_center(cell_i) / (get_grid_end() - get_grid_start());
		gnuplot_file << x << " " << vy << "\n";
	}
	gnuplot_file << "end\n";
	// vz
	for (size_t cell_i = 0; cell_i < std::tuple_size<Grid>::value; cell_i++) {
		auto& cell_data = grid[cell_i];
		double vz = 0;
		if (Mas(cell_data) > 0) {
			vz = Mom(cell_data)[2] / Mas(cell_data);
		}
		const double x = get_cell_center(cell_i) / (get_grid_end() - get_grid_start());
		gnuplot_file << x << " " << vz << "\n";
	}
	gnuplot_file << "end\n";

	gnuplot_file
		<< "set term png enhanced\nset output '"
		<< B_plot_file_name
		<< "'\nset xlabel 'position'\n"
		   "set ylabel 'magnetic field'\n"
		   "set key horizontal outside bottom\n"
		   "plot "
		     "'-' u 1:2 w l lw 2 t 'B_x', "
		     "'-' u 1:2 w l lw 2 t 'B_y', "
		     "'-' u 1:2 w l lw 2 t 'B_z'\n"
		     ;
	// Bx
	for (size_t cell_i = 0; cell_i < std::tuple_size<Grid>::value; cell_i++) {
		const double x
			= get_cell_center(cell_i)
			/ (get_grid_end() - get_grid_start());
		gnuplot_file << x << " "
			<< Mag(grid[cell_i])[0] << "\n";
	}
	gnuplot_file << "end\n";
	// By
	for (size_t cell_i = 0; cell_i < std::tuple_size<Grid>::value; cell_i++) {
		const double x
			= get_cell_center(cell_i)
			/ (get_grid_end() - get_grid_start());
		gnuplot_file << x << " "
			<< Mag(grid[cell_i])[1] << "\n";
	}
	gnuplot_file << "end\n";
	// Bz
	for (size_t cell_i = 0; cell_i < std::tuple_size<Grid>::value; cell_i++) {
		const double x
			= get_cell_center(cell_i)
			/ (get_grid_end() - get_grid_start());
		gnuplot_file << x << " "
			<< Mag(grid[cell_i])[2] << "\n";
	}
	gnuplot_file << "end\n";

	return gnuplot_file_name;
}

/*!
Writes given advection simulation to an ascii file.

Returns the name of the file written which
is derived from given solver name.
*/
template <
	class MHD_T,
	class Mass_Density_T,
	class Momentum_Density_T,
	class Total_Energy_Density_T,
	class Magnetic_Field_T
> void save_mhd(
	const std::string& solver,
	const Grid& grid
) {
	const std::string file_name("mhd_" + solver + ".dat");

	std::ofstream outfile(file_name);
	outfile << std::setprecision(16) << std::scientific;

	for (const auto& cell_data: grid) {
		const auto& state = cell_data[MHD_T()];
		outfile
			<< state[Mass_Density_T()] << " "
			<< state[Momentum_Density_T()][0] << " "
			<< state[Momentum_Density_T()][1] << " "
			<< state[Momentum_Density_T()][2] << " "
			<< state[Total_Energy_Density_T()] << " "
			<< state[Magnetic_Field_T()][0] << " "
			<< state[Magnetic_Field_T()][1] << " "
			<< state[Magnetic_Field_T()][2] << "\n";
	}
	outfile << std::endl;
}


template <class T> T get_relative_error(const T a, const T b)
{
	if (a == T(0) && b == T(0)) {
		return {0};
	}

	return {std::fabs(a - b) / std::max(std::fabs(a), std::fabs(b))};
}


template <
	class MHD_T,
	class Mass_Density_T,
	class Momentum_Density_T,
	class Total_Energy_Density_T,
	class Magnetic_Field_T
> void verify_mhd(
	const Grid& grid,
	const std::string& file_name
) {
	std::ifstream infile(file_name);
	if (not infile.good()) {
		// try a subdirectory in case run from repository root
		infile.open("tests/mhd/" + file_name);

		if (not infile.good()) {
			std::cerr <<  __FILE__ << ":" << __LINE__
				<< " Couldn't open file " << file_name
				<< std::endl;
			abort();
		}
	}

	constexpr double
		// maximum allowed relative difference from reference
		max_error = 1e-6,
		// values smaller than this are assumed correct
		min_value = 1e-25;

	for (size_t cell_i = 0; cell_i < std::tuple_size<Grid>::value; cell_i++) {
		typename Mass_Density_T::data_type ref_rho;
		typename Momentum_Density_T::data_type ref_mom;
		typename Total_Energy_Density_T::data_type ref_nrj;
		typename Magnetic_Field_T::data_type ref_mag;

		infile
			>> ref_rho
			>> ref_mom[0]
			>> ref_mom[1]
			>> ref_mom[2]
			>> ref_nrj
			>> ref_mag[0]
			>> ref_mag[1]
			>> ref_mag[2];

		const auto& state = grid[cell_i][MHD_T()];
		const auto rho = state[Mass_Density_T()];
		const auto mom = state[Momentum_Density_T()];
		const auto nrj = state[Total_Energy_Density_T()];
		const auto mag = state[Magnetic_Field_T()];

		if (
			(rho > min_value or ref_rho > min_value)
			and get_relative_error(rho, ref_rho) > max_error
		) {
			std::cerr <<  __FILE__ << ":" << __LINE__
				<< " density " << rho << " != " << ref_rho
				<< " at cell " << cell_i
				<< std::endl;
			abort();
		}

		if (
			(mom[0] > min_value or ref_mom[0] > min_value)
			and get_relative_error(mom[0], ref_mom[0]) > max_error
		) {
			std::cerr <<  __FILE__ << ":" << __LINE__
				<< " x momentum " << mom[0] << " != " << ref_mom[0]
				<< " at cell " << cell_i
				<< std::endl;
			abort();
		}
		if (
			(mom[1] > min_value or ref_mom[1] > min_value)
			and get_relative_error(mom[1], ref_mom[1]) > max_error
		) {
			std::cerr <<  __FILE__ << ":" << __LINE__
				<< " y momentum " << mom[1] << " != " << ref_mom[1]
				<< " at cell " << cell_i
				<< std::endl;
			abort();
		}
		if (
			(mom[2] > min_value or ref_mom[2] > min_value)
			and get_relative_error(mom[2], ref_mom[2]) > max_error
		) {
			std::cerr <<  __FILE__ << ":" << __LINE__
				<< " z momentum " << mom[2] << " != " << ref_mom[2]
				<< " at cell " << cell_i
				<< std::endl;
			abort();
		}

		if (
			(nrj > min_value or ref_nrj > min_value)
			and get_relative_error(nrj, ref_nrj) > max_error
		) {
			std::cerr <<  __FILE__ << ":" << __LINE__
				<< " energy " << nrj << " != " << ref_nrj
				<< " at cell " << cell_i
				<< std::endl;
			abort();
		}

		if (
			(mag[0] > min_value or ref_mag[0] > min_value)
			and get_relative_error(mag[0], ref_mag[0]) > max_error
		) {
			std::cerr <<  __FILE__ << ":" << __LINE__
				<< " x magnetic field " << mag[0] << " != " << ref_mag[0]
				<< " at cell " << cell_i
				<< std::endl;
			abort();
		}
		if (
			(mag[1] > min_value or ref_mag[1] > min_value)
			and get_relative_error(mag[1], ref_mag[1]) > max_error
		) {
			std::cerr <<  __FILE__ << ":" << __LINE__
				<< " y magnetic field " << mag[1] << " != " << ref_mag[1]
				<< " at cell " << cell_i
				<< std::endl;
			abort();
		}
		if (
			(mag[2] > min_value or ref_mag[2] > min_value)
			and get_relative_error(mag[2], ref_mag[2]) > max_error
		) {
			std::cerr <<  __FILE__ << ":" << __LINE__
				<< " z magnetic field " << mag[2] << " != " << ref_mag[2]
				<< " at cell " << cell_i
				<< std::endl;
			abort();
		}
	}
}


int main(int argc, char* argv[])
{
	bool save = false, plot = false, no_verify = false, verbose = false;
	std::string solver_str("hll_athena");
	boost::program_options::options_description options(
		"Usage: program_name [options], where options are:"
	);
	options.add_options()
		("help", "Print this help message")
		("solver",
			boost::program_options::value<std::string>(&solver_str)
				->default_value(solver_str),
			"Solver to use, available: hll_athena")
		("save", "Save end result to ascii file")
		("plot", "Plot results using gnuplot")
		("no-verify", "Do not verify against reference results")
		("verbose", "Print run time information");

	// read options from command line
	boost::program_options::variables_map option_variables;
	boost::program_options::store(boost::program_options::parse_command_line(argc, argv, options), option_variables);
	boost::program_options::notify(option_variables);

	if (option_variables.count("help") > 0) {
		cout << options << endl;
		return EXIT_SUCCESS;
	}

	if (option_variables.count("save") > 0) {
		save = true;
	}

	if (option_variables.count("plot") > 0) {
		plot = true;
	}

	if (option_variables.count("no-verify") > 0) {
		no_verify = true;
	}

	if (option_variables.count("verbose") > 0) {
		verbose = true;
	}

	const auto solver
		= [&solver_str](){
			if (solver_str == "hll_athena") {

				return pamhd::mhd::athena::get_flux_hll<
					pamhd::mhd::MHD_State_Conservative::data_type,
					pamhd::mhd::Mass_Density,
					pamhd::mhd::Momentum_Density,
					pamhd::mhd::Total_Energy_Density,
					pamhd::mhd::Magnetic_Field
				>;

			} else if (solver_str == "hlld_athena") {

				return pamhd::mhd::athena::get_flux_hlld<
					pamhd::mhd::MHD_State_Conservative::data_type,
					pamhd::mhd::Mass_Density,
					pamhd::mhd::Momentum_Density,
					pamhd::mhd::Total_Energy_Density,
					pamhd::mhd::Magnetic_Field
				>;

			} else if (solver_str == "roe_athena") {

				return pamhd::mhd::athena::get_flux_roe<
					pamhd::mhd::MHD_State_Conservative::data_type,
					pamhd::mhd::Mass_Density,
					pamhd::mhd::Momentum_Density,
					pamhd::mhd::Total_Energy_Density,
					pamhd::mhd::Magnetic_Field
				>;

			} else {

				std::cerr <<  __FILE__ << "(" << __LINE__ << ") Invalid solver: "
					<< solver_str << ", use --help to list available solvers"
					<< std::endl;
				abort();
			}
		}();

	Grid grid;

	if (verbose) {
		cout << "Initializing MHD" << endl;
	}
	initialize_mhd(grid, adiabatic_index, vacuum_permeability);
	for (auto& cell: grid) {
		Mas(cell) = Mas1(cell) + Mas2(cell);
		Mom(cell) = Mom1(cell) + Mom2(cell);
		Nrj(cell) = Nrj1(cell) + Nrj2(cell);
	}

	if (plot) {
		std::array<double, 3> empty{{0, 0, 0}};
		const auto empty_mag = [&empty](Cell&){return empty;};
		const std::string
			mhd_gnuplot_file_name
				= plot_mhd(
					"2mhd_",
					grid,
					0,
					adiabatic_index,
					vacuum_permeability,
					Mas, Mom, Nrj, Mag
				),
			hd1_gnuplot_file_name
				= plot_mhd(
					"hd1_",
					grid,
					0,
					adiabatic_index,
					vacuum_permeability,
					Mas1, Mom1, Nrj1, empty_mag
				),
			hd2_gnuplot_file_name
				= plot_mhd(
					"hd2_",
					grid,
					0,
					adiabatic_index,
					vacuum_permeability,
					Mas2, Mom2, Nrj2, empty_mag
				);

		system(("gnuplot " + mhd_gnuplot_file_name).c_str());
		system(("gnuplot " + hd1_gnuplot_file_name).c_str());
		system(("gnuplot " + hd2_gnuplot_file_name).c_str());
	}

	const double
		simulation_duration = 1,
		mhd_plot_interval = simulation_duration / 5;
	double
		max_dt = 0,
		simulation_time = 0,
		mhd_next_plot = mhd_plot_interval;
	while (simulation_time < simulation_duration) {

		const double
			CFL = 0.5,
			// don't step over the final simulation time
			until_end = simulation_duration - simulation_time,
			time_step = std::min(CFL * max_dt, until_end);

		max_dt = std::numeric_limits<double>::max();

		if (verbose) {
			cout << "Solving MHD at time " << simulation_time
				<< " s with time step " << time_step << " s" << endl;
		}

		max_dt = std::min(
			max_dt,
			solve_mhd(
				solver,
				grid,
				time_step,
				adiabatic_index,
				vacuum_permeability,
				Mas, Mom, Nrj, Mag,
				Mas_f, Mom_f, Nrj_f, Mag_f
			)
		);

		/*
		Apply & zero fluxes.

		Each side of a face contributes by its mass fraction.
		*/
		const double
			cell_size = get_cell_size(),
			face_area = 1.0,
			volume = face_area * cell_size;

		for (auto& cell: grid) {
			const auto
				mass_fraction1 = Mas1(cell) / Mas(cell),
				mass_fraction2 = Mas2(cell) / Mas(cell);

			pamhd::mhd::apply_fluxes(
				cell,
				mass_fraction1 / volume,
				adiabatic_index,
				vacuum_permeability,
				Mas1, Mom1, Nrj1, Mag,
				Mas_f, Mom_f, Nrj_f, Mag_f,
				false
			);
			pamhd::mhd::apply_fluxes(
				cell,
				mass_fraction2 / volume,
				adiabatic_index,
				vacuum_permeability,
				Mas2, Mom2, Nrj2, Mag,
				Mas_f, Mom_f, Nrj_f, Mag_f,
				false
			);

			Mas_f(cell)    =
			Mom_f(cell)[0] =
			Mom_f(cell)[1] =
			Mom_f(cell)[2] =
			Nrj_f(cell)    =
			Mag_f(cell)[0] =
			Mag_f(cell)[1] =
			Mag_f(cell)[2] = 0;
		}

		// make new total state, zero fluxes
		for (auto& cell: grid) {
			Mas(cell) = Mas1(cell) + Mas2(cell);
			Mom(cell) = Mom1(cell) + Mom2(cell);
			Nrj(cell) = Nrj1(cell) + Nrj2(cell);
		}

		simulation_time += time_step;

		if (plot && mhd_next_plot <= simulation_time) {
			mhd_next_plot += mhd_plot_interval;

			std::array<double, 3> empty{{0, 0, 0}};
			const auto empty_mag = [&empty](Cell&){return empty;};

			const std::string
				mhd_gnuplot_file_name
					= plot_mhd(
						"2mhd_",
						grid,
						simulation_time,
						adiabatic_index,
						vacuum_permeability,
						Mas, Mom, Nrj, Mag
					),
				hd1_gnuplot_file_name
					= plot_mhd(
						"hd1_",
						grid,
						simulation_time,
						adiabatic_index,
						vacuum_permeability,
						Mas1, Mom1, Nrj1, empty_mag
					),
				hd2_gnuplot_file_name
					= plot_mhd(
						"hd2_",
						grid,
						simulation_time,
						adiabatic_index,
						vacuum_permeability,
						Mas2, Mom2, Nrj2, empty_mag
					);

			system(("gnuplot " + mhd_gnuplot_file_name).c_str());
			system(("gnuplot " + hd1_gnuplot_file_name).c_str());
			system(("gnuplot " + hd2_gnuplot_file_name).c_str());
		}
	}

	if (save) {
		if (verbose) {
			cout << "Saving MHD at time " << simulation_time << endl;
		}
		save_mhd<
			pamhd::mhd::MHD_State_Conservative,
			pamhd::mhd::Mass_Density,
			pamhd::mhd::Momentum_Density,
			pamhd::mhd::Total_Energy_Density,
			pamhd::mhd::Magnetic_Field
		>(solver_str, grid);
	}

	if (not no_verify) {
		const std::string reference_name("mhd_" + solver_str + ".ref");

		if (verbose) {
			cout << "Verifying result against file " << reference_name << endl;
		}

		verify_mhd<
			pamhd::mhd::MHD_State_Conservative,
			pamhd::mhd::Mass_Density,
			pamhd::mhd::Momentum_Density,
			pamhd::mhd::Total_Energy_Density,
			pamhd::mhd::Magnetic_Field
		>(grid, reference_name);
	}

	if (verbose) {
		cout << "Simulation finished at time " << simulation_time << endl;
	}

	return EXIT_SUCCESS;
}
