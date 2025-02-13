/*
 * Copyright (c) 2018, Houston Mechatronics Inc., JD Yamokoski
 * Copyright (c) 2012, Clearpath Robotics, Inc., Alex Bencz
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <iostream>

// Local includes
#include <mocap_optitrack/socket.h>
#include <mocap_optitrack/data_model.h>
#include <mocap_optitrack/mocap_config.h>
#include <mocap_optitrack/rigid_body_publisher.h>
#include <mocap_optitrack/MocapOptitrackConfig.h>
#include "natnet/natnet_messages.h"

#include <dynamic_reconfigure/server.h>
#include <memory>
#include <ros/ros.h>


namespace mocap_optitrack
{

class OptiTrackRosBridge
{
public:
  OptiTrackRosBridge(ros::NodeHandle& nh,
                     ServerDescription const& serverDescr,
                     PublisherConfigurations const& pubConfigs) :
    nh(nh), server(ros::NodeHandle("~/optitrack_config"))
  {
    server.setCallback(boost::bind(&OptiTrackRosBridge::reconfigureCallback, this, _1, _2));
    serverDescription = serverDescr;
    publisherConfigurations = pubConfigs;
  }

  void reconfigureCallback(MocapOptitrackConfig& config, uint32_t)
  {
    serverDescription.enableOptitrack = config.enable_optitrack;
    serverDescription.commandPort = config.command_port;
    serverDescription.dataPort = config.data_port;
    serverDescription.multicastIpAddress = config.multicast_address;

    initialize();
  }

  void initialize()
  {
    if (serverDescription.enableOptitrack)
    {
      // Create socket
      multicastClientSocketPtr.reset(
        new UdpMulticastSocket(serverDescription.dataPort,
                               serverDescription.multicastIpAddress));

      if (!serverDescription.version.empty())
      {
        dataModel.setVersions(&serverDescription.version[0], &serverDescription.version[0]);
      }

      // Need verion information from the server to properly decode any of their packets.
      // If we have not recieved that yet, send another request.
      while (ros::ok() && !dataModel.hasServerInfo())
      {
        natnet::ConnectionRequestMessage connectionRequestMsg;
        natnet::MessageBuffer connectionRequestMsgBuffer;
        connectionRequestMsg.serialize(connectionRequestMsgBuffer, NULL);
        int ret = multicastClientSocketPtr->send(
                    &connectionRequestMsgBuffer[0],
                    connectionRequestMsgBuffer.size(),
                    serverDescription.commandPort);
        if (updateDataModelFromServer()) usleep(10);
        else sleep(1);

        ros::spinOnce();
      }
      // Once we have the server info, create publishers
      publishDispatcherPtr.reset(
        new RigidBodyPublishDispatcher(nh,
                                       dataModel.getNatNetVersion(),
                                       publisherConfigurations));
      ROS_INFO("Initialization complete");
      initialized = true;
    }
    else
    {
      ROS_INFO("Initialization incomplete");
      initialized = false;
    }
  };

  void run()
  {
    while (ros::ok())
    {
      if (initialized)
      {
        if (updateDataModelFromServer())
        {
          // Maybe we got some data? If we did it would be in the form of one or more
          // rigid bodies in the data model
          ros::Time time = ros::Time::now();
          publishDispatcherPtr->publish(time, dataModel.dataFrame.rigidBodies);

          // Clear out the model to prepare for the next frame of data
          dataModel.clear();
        }
        // whether receive or nor, give a short break to relieft the CPU load due to while()
        usleep(100);
      }
      else
      {
        ros::Duration(1.).sleep();
      }
      ros::spinOnce();
    }
  }

private:
  bool updateDataModelFromServer()
  {
    // Get data from mocap server
    int numBytesReceived = multicastClientSocketPtr->recv();
    if (numBytesReceived > 0)
    {
      // Grab latest message buffer
      const char* pMsgBuffer = multicastClientSocketPtr->getBuffer();

      // Copy char* buffer into MessageBuffer and dispatch to be deserialized
      natnet::MessageBuffer msgBuffer(pMsgBuffer, pMsgBuffer + numBytesReceived);
      natnet::MessageDispatcher::dispatch(msgBuffer, &dataModel);

      return true;
    }

    return false;
  };

  ros::NodeHandle& nh;
  ServerDescription serverDescription;
  PublisherConfigurations publisherConfigurations;
  DataModel dataModel;
  std::unique_ptr<UdpMulticastSocket> multicastClientSocketPtr;
  std::unique_ptr<RigidBodyPublishDispatcher> publishDispatcherPtr;
  dynamic_reconfigure::Server<MocapOptitrackConfig> server;
  bool initialized;
};

}  // namespace mocap_optitrack


////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
{
  // std::cout << "SADLIFJHASLDFJKNASLDKFJNASLDKFJNASLDKFJNASDLÒKFJNASDLKFJNASDÒKFJN\n\n\n\n\nlaishdfasljdhfbasljdhasldfhasldfh";
  // Initialize ROS node
  ros::init(argc, argv, "mocap_node");
  ros::NodeHandle nh("~");

  // Grab node configuration from rosparam
  mocap_optitrack::ServerDescription serverDescription;
  mocap_optitrack::PublisherConfigurations publisherConfigurations;
  mocap_optitrack::NodeConfiguration::fromRosParam(nh, serverDescription, publisherConfigurations);

  // Create node object, initialize and run
  mocap_optitrack::OptiTrackRosBridge node(nh, serverDescription, publisherConfigurations);
  node.initialize();
  node.run();

  return 0;
}
