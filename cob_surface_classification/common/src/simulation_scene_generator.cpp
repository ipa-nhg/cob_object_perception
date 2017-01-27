/*!
 *****************************************************************
 * \file
 *
 * \note
 * Copyright (c) 2015 \n
 * Fraunhofer Institute for Manufacturing Engineering
 * and Automation (IPA) \n\n
 *
 *****************************************************************
 *
 * \note
 * Project name: Care-O-bot
 * \note
 * ROS stack name: cob_object_perception
 * \note
 * ROS package name: cob_surface_classification
 *
 * \author
 * Author: Richard Bormann
 * \author
 * Supervised by:
 *
 * \date Date of creation: 24.02.2015
 *
 * \brief
 *
 *
 *****************************************************************
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer. \n
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution. \n
 * - Neither the name of the Fraunhofer Institute for Manufacturing
 * Engineering and Automation (IPA) nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission. \n
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License LGPL as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License LGPL for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License LGPL along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 *
 ****************************************************************/

#include "stdlib.h"
#include <iostream>
#include "string.h"
#include <sstream>
#include <vector>
#include <fstream>

struct BoundingVolume
{
	double min_x;
	double max_x;
	double min_y;
	double max_y;
	double min_z;
	double max_z;

	BoundingVolume()
	{
		min_x = max_x = min_y = max_y = min_z = max_z = 0.;
	}

	BoundingVolume(double min_x_, double max_x_, double min_y_, double max_y_, double min_z_, double max_z_)
	{
		setTo(min_x_, max_x_, min_y_, max_y_, min_z_, max_z_);
	}

	void setTo(double min_x_, double max_x_, double min_y_, double max_y_, double min_z_, double max_z_)
	{
		min_x = min_x_;
		max_x = max_x_;
		min_y = min_y_;
		max_y = max_y_;
		min_z = min_z_;
		max_z = max_z_;
	}
};

double getRandomCoordinate(const double min_val, const double max_val)
{
	return min_val + (max_val-min_val) * ((double)rand()/(double)RAND_MAX);
}

std::string addObject(const std::string& name, const BoundingVolume& limits, const BoundingVolume& angle_limits, const int index)
{
	std::stringstream ss;

	ss << "\n\t<param name=\"" << name << index << "\" textfile=\"$(find cob_surface_classification)/models/objects/" << name << ".urdf\" />\n"
		<< "\t<node name=\"spawn_" << name << index << "\" pkg=\"gazebo_ros\" type=\"spawn_model\" args=\" -param " << name << index
		<< " -urdf -x " << getRandomCoordinate(limits.min_x, limits.max_x)
		<< " -y " << getRandomCoordinate(limits.min_y, limits.max_y)
		<< " -z " << getRandomCoordinate(limits.min_z, limits.max_z)
		<< "  -R " << getRandomCoordinate(angle_limits.min_x, angle_limits.max_x)
		<< " -P " << getRandomCoordinate(angle_limits.min_x, angle_limits.max_x)
		<< " -Y " << getRandomCoordinate(angle_limits.min_x, angle_limits.max_x)
		<< " -model " << name << index << "\" respawn=\"false\" output=\"screen\" />\n";

	return ss.str();

//	<param name="cylinder" textfile="$(find cob_surface_classification)/models/objects/cylinder.urdf" />
//	<node name="spawn_cylinder" pkg="gazebo_ros" type="spawn_model" args=" -param cylinder -urdf -x -1.2 -y 0.4 -z 0.65  -R 0 -P 0 -Y 0 -model cylinder" respawn="false" output="screen" />
//
//	<param name="cone" textfile="$(find cob_surface_classification)/models/objects/cone.urdf" />
//	<node name="spawn_cone" pkg="gazebo_ros" type="spawn_model" args=" -param cone -urdf -x -1.1 -y 0.05 -z 0.65  -R 0 -P 0 -Y 0 -model cone" respawn="false" output="screen" />
//
//	<param name="cylinder_hollow" textfile="$(find cob_surface_classification)/models/objects/cylinder_hollow.urdf" />
//	<node name="spawn_cylinder_hollow" pkg="gazebo_ros" type="spawn_model" args=" -param cylinder_hollow -urdf -x -1.0 -y 0.2 -z 0.65  -R 0 -P 0 -Y 0 -model cylinder_hollow" respawn="false" output="screen" />
//
//	<param name="box_small" textfile="$(find cob_surface_classification)/models/objects/box_small.urdf" />
//	<node name="spawn_box_small" pkg="gazebo_ros" type="spawn_model" args=" -param box_small -urdf -x -1.1 -y -0.3 -z 0.65  -R 0 -P 0 -Y 0 -model box_small" respawn="false" output="screen" />
//
//	<param name="box_flat" textfile="$(find cob_surface_classification)/models/objects/box_flat.urdf" />
//	<node name="spawn_box_flat" pkg="gazebo_ros" type="spawn_model" args=" -param box_flat -urdf -x -1.0 -y -0.6 -z 0.65  -R 0 -P 0 -Y 0 -model box_flat" respawn="false" output="screen" />
}

int main (int argc, char** argv)
{
	// configuration
	enum RobotConfig {TORSO_BACK, TORSO_HOME, TORSO_BACK_EXTREME};
	RobotConfig config = TORSO_BACK;
	int file_index_start = 17;
	int number_files_to_generate = 28;
	if (argc >= 2)
	{
		if (strcmp(argv[1], "back")==0)
			config = TORSO_BACK;
		else if (strcmp(argv[1], "home")==0)
			config = TORSO_HOME;
		else if (strcmp(argv[1], "back_extreme")==0)
			config = TORSO_BACK_EXTREME;
	}
	if (argc >= 3)
	{
		std::stringstream ss;
		ss << argv[2];
		ss >> file_index_start;
	}
	if (argc >= 4)
	{
		std::stringstream ss;
		ss << argv[3];
		ss >> number_files_to_generate;
	}

	std::vector<std::string> config_names;
	config_names.push_back("torso_back");
	config_names.push_back("torso_home");
	config_names.push_back("torso_back_extreme");
	std::cout << "Generating " << number_files_to_generate << " files starting with index " << file_index_start << " for configuration " << config_names[config] << ". Hit any key to proceed.\n";
	getchar();

	std::vector<std::string> object_names;
	object_names.push_back("cone");
	object_names.push_back("cone");
	object_names.push_back("cylinder");
	object_names.push_back("cylinder");
	object_names.push_back("cylinder_hollow");
	object_names.push_back("cylinder_hollow");
	object_names.push_back("box_small");
	object_names.push_back("box_small");
	object_names.push_back("box_flat");
	object_names.push_back("box_flat");
	object_names.push_back("cup_and_saucer");
	object_names.push_back("cup_large");

	srand(0);

	BoundingVolume bounding_volume;
	BoundingVolume angle_limits0(0,0,0,0,0,0);
	BoundingVolume angle_limits(-1.57,1.57,-1.57,1.57,-1.57,1.57);
	std::string preamble;
	if (config == TORSO_BACK)
	{
		// variant torso back
		bounding_volume.setTo(-1.8, -0.8, -0.6, 0.4, 0.65, 1.05);
		std::stringstream ss;
		ss << "<launch>\n\n\t<!-- to be used with robot config: torso = back -->\n\n"
				<< "\t<!-- send urdf to param server -->\n"
				<< "\t<param name=\"bottom_plane\" textfile=\"$(find cob_surface_classification)/models/objects/bottom_plane.urdf\" />\n"
				<< "\t<!-- push urdf to factory and spawn robot in gazebo -->\n"
				<< "\t<node name=\"spawn_bottom_plane\" pkg=\"gazebo_ros\" type=\"spawn_model\" args=\" -param bottom_plane -urdf -x -10.5 -y 0 -z 0  -R 0 -P 0 -Y 0 -model bottom_plane\" respawn=\"false\" output=\"screen\" />\n"
				<< "\t<param name=\"floor\" textfile=\"$(find cob_surface_classification)/models/objects/floor.urdf\" />\n"
				<< "\t<node name=\"spawn_floor\" pkg=\"gazebo_ros\" type=\"spawn_model\" args=\" -param floor -urdf -x -1.3 -y 0 -z -0.1 -R 0 -P 0 -Y 0 -model floor\" respawn=\"false\" output=\"screen\" />\n"
				<< "\t<param name=\"wall\" textfile=\"$(find cob_surface_classification)/models/objects/wall.urdf\" />\n"
				<< "\t<node name=\"spawn_wall\" pkg=\"gazebo_ros\" type=\"spawn_model\" args=\" -param wall -urdf -x -3.1 -y 0 -z 0.15 -R 0 -P 0 -Y 0 -model wall\" respawn=\"false\" output=\"screen\" />\n\n";
		preamble = ss.str();
	}
	else if (config == TORSO_HOME)
	{
		// variant torso back
		bounding_volume.setTo(-2.8, -1.8, -1.2, 0.8, 0.65, 1.15);
		std::stringstream ss;
		ss << "<launch>\n\n\t<!-- to be used with robot config: torso = home -->\n\n"
				<< "\t<!-- send urdf to param server -->\n"
				<< "\t<param name=\"bottom_plane\" textfile=\"$(find cob_surface_classification)/models/objects/bottom_plane.urdf\" />\n"
				<< "\t<!-- push urdf to factory and spawn robot in gazebo -->\n"
				<< "\t<node name=\"spawn_bottom_plane\" pkg=\"gazebo_ros\" type=\"spawn_model\" args=\" -param bottom_plane -urdf -x -10.5 -y 0 -z 0  -R 0 -P 0 -Y 0 -model bottom_plane\" respawn=\"false\" output=\"screen\" />\n"
				<< "\t<param name=\"floor\" textfile=\"$(find cob_surface_classification)/models/objects/floor.urdf\" />\n"
				<< "\t<node name=\"spawn_floor\" pkg=\"gazebo_ros\" type=\"spawn_model\" args=\" -param floor -urdf -x -2.3 -y 0 -z -0.1 -R 0 -P 0 -Y 0 -model floor\" respawn=\"false\" output=\"screen\" />\n"
				<< "\t<param name=\"wall\" textfile=\"$(find cob_surface_classification)/models/objects/wall.urdf\" />\n"
				<< "\t<node name=\"spawn_wall\" pkg=\"gazebo_ros\" type=\"spawn_model\" args=\" -param wall -urdf -x -3.6 -y 0 -z 0.15 -R 0 -P 0 -Y 0 -model wall\" respawn=\"false\" output=\"screen\" />\n\n";
		preamble = ss.str();
	}
	else if (config == TORSO_BACK_EXTREME)
	{
		// variant torso back
		bounding_volume.setTo(-1.8, -0.8, -0.6, 0.4, 0.15, 0.55);
		std::stringstream ss;
		ss << "<launch>\n\n\t<!-- to be used with robot config: torso = back_extreme -->\n\n"
				<< "\t<!-- send urdf to param server -->\n"
				<< "\t<param name=\"bottom_plane\" textfile=\"$(find cob_surface_classification)/models/objects/bottom_plane.urdf\" />\n"
				<< "\t<!-- push urdf to factory and spawn robot in gazebo -->\n"
				<< "\t<node name=\"spawn_bottom_plane\" pkg=\"gazebo_ros\" type=\"spawn_model\" args=\" -param bottom_plane -urdf -x -10.2 -y 0 -z -0.1  -R 0 -P 0 -Y 0 -model bottom_plane\" respawn=\"false\" output=\"screen\" />\n"
				<< "\t<param name=\"floor\" textfile=\"$(find cob_surface_classification)/models/objects/floor.urdf\" />\n"
				<< "\t<node name=\"spawn_floor\" pkg=\"gazebo_ros\" type=\"spawn_model\" args=\" -param floor -urdf -x -1.3 -y 0 -z -0.6 -R 0 -P 0 -Y 0 -model floor\" respawn=\"false\" output=\"screen\" />\n"
				<< "\t<param name=\"wall\" textfile=\"$(find cob_surface_classification)/models/objects/wall.urdf\" />\n"
				<< "\t<node name=\"spawn_wall\" pkg=\"gazebo_ros\" type=\"spawn_model\" args=\" -param wall -urdf -x -3.1 -y 0 -z 0.15 -R 0 -P 0 -Y 0 -model wall\" respawn=\"false\" output=\"screen\" />\n\n";
		preamble = ss.str();
	}
	else
	{
		std::cout << "Error: wrong configuration." << std::endl;
		return(0);
	}

	const int file_index_end = file_index_start + number_files_to_generate;
	for (int file_index=file_index_start; file_index<file_index_end; ++file_index)
	{
		std::stringstream ss;
		ss << preamble;
		const int number_objects = 10+rand()%40;
		for (int i=1; i<=number_objects; ++i)
		{
			int object_index = rand()%object_names.size();
			ss << addObject(object_names[object_index], bounding_volume, (rand()%2==0 ? angle_limits0 : angle_limits), i);
		}
		ss << "\n</launch>\n";

		//std::cout << "File " << file_index << ":\n" << ss.str() << std::endl;

		std::stringstream filename;
		filename << "sim_gen_" << (file_index<1000 ? "0" : "") << (file_index<100 ? "0" : "") << (file_index<10 ? "0" : "") << file_index << ".launch";
		std::ofstream file(filename.str().c_str(), std::ios::out);
		if (file.is_open())
			file << ss.str();
		file.close();
	}

	return (0);
}
