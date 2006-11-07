/***************************************************************************
 tag: Wim Meeussen and Johan Rutgeerts  Mon Jan 19 14:11:20 CET 2004   
       Ruben Smits Fri 12 08:31 CET 2006
                           -------------------
    begin                : Mon January 19 2004
    copyright            : (C) 2004 Peter Soetens
    email                : first.last@mech.kuleuven.ac.be
 
 ***************************************************************************
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Lesser General Public            *
 *   License as published by the Free Software Foundation; either          *
 *   version 2.1 of the License, or (at your option) any later version.    *
 *                                                                         *
 *   This library is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU     *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with this library; if not, write to the Free Software   *
 *   Foundation, Inc., 59 Temple Place,                                    *
 *   Suite 330, Boston, MA  02111-1307  USA                                *
 *                                                                         *
 ***************************************************************************/ 

#include "Kuka361nAxesTorqueController.hpp"

#include <rtt/Logger.hpp>

namespace OCL
{
    using namespace RTT;
    using namespace std;
  
#define KUKA361_NUM_AXES 6

#define KUKA361_ENCODEROFFSETS { 1000004, 1000000, 1000002, 449784, 1035056, 1230656 }

#define KUKA361_CONV1  94.14706
#define KUKA361_CONV2  -103.23529
#define KUKA361_CONV3  51.44118
#define KUKA361_CONV4  175
#define KUKA361_CONV5  150
#define KUKA361_CONV6  131.64395

#define KUKA361_ENC_RES  4096

  // Conversion from encoder ticks to radiants
#define KUKA361_TICKS2RAD { 2*M_PI / (KUKA361_CONV1 * KUKA361_ENC_RES), 2*M_PI / (KUKA361_CONV2 * KUKA361_ENC_RES), 2*M_PI / (KUKA361_CONV3 * KUKA361_ENC_RES), 2*M_PI / (KUKA361_CONV4 * KUKA361_ENC_RES), 2*M_PI / (KUKA361_CONV5 * KUKA361_ENC_RES), 2*M_PI / (KUKA361_CONV6 * KUKA361_ENC_RES)}
  
  // Conversion from angular speed to voltage
#define KUKA361_RADproSEC2VOLT { 2.5545, 2.67804024532652, 1.37350318088664, 2.34300679603342, 2.0058, 3.3786 } //18 april 2006

	// Conversion factors for tacho, unknown for last 3 axes
#define KUKA361_TACHOSCALE { 9.2750, 10.0285, 4.9633, 5, 5, 5 } //oktober 2006
#define KUKA361_TACHOOFFSET { 0.0112, 0.0083, 0.0056, 0, 0, 0 } //oktober 2006

	// Conversion of current to torque: Km. Initial values
#define KUKA361_KM { 0.2781*5.77*94.14706, 0.2863*5.85*103.23529, 0.2887*5.78*51.44118, 0.07*5.7*175, 0.07*5.7*150, 0.07*5.7*131.64395 } //oktober 2006

	// parameters of current regulator: I = (a*UN + b)/R, unknown for last 3 axes
#define KUKA361_A { -0.7874,  -0.7904, -0.7890, -0.7890, -0.7890, -0.7890 } //oktober 2006
#define KUKA361_B { -0.0248,  0.0091, -0.0624, -0.0624, -0.0624, -0.0624 } //oktober 2006
#define KUKA361_R { 0.1733,  0.1709, 0.1730, 0.1730, 0.1730, 0.1730 } //oktober 2006

  // Channel position offset on DAQ-boards
#define TACHO_OFFSET 0 //oktober 2006
#define CURRENT_OFFSET KUKA361_NUM_AXES //oktober 2006
#define	MODE_OFFSET 0 //oktober 2006

	// Define which axes should be torque controlled
#define	TORQUE_CONTROLLED { 1, 1, 1, 1, 1, 1 }  //oktober 2006

    typedef Kuka361nAxesTorqueController MyType;
    
    Kuka361nAxesTorqueController::Kuka361nAxesTorqueController(string name,string propertyfile)
        : TaskContext(name),
          _startAxis( "startAxis", &MyType::startAxis,&MyType::startAxisCompleted, this),
          _startAllAxes( "startAllAxes", &MyType::startAllAxes,&MyType::startAllAxesCompleted, this),
          _stopAxis( "stopAxis", &MyType::stopAxis,&MyType::stopAxisCompleted, this),
          _stopAllAxes( "stopAllAxes", &MyType::stopAllAxes,&MyType::stopAllAxesCompleted, this),
          _unlockAxis( "unlockAxis", &MyType::unlockAxis,&MyType::unlockAxisCompleted, this),
          _unlockAllAxes( "unlockAllAxes", &MyType::unlockAllAxes,&MyType::unlockAllAxesCompleted, this),
          _lockAxis( "lockAxis", &MyType::lockAxis,&MyType::lockAxisCompleted, this),
          _lockAllAxes( "lockAllAxes", &MyType::lockAllAxes,&MyType::lockAllAxesCompleted, this),
          _prepareForUse( "prepareForUse", &MyType::prepareForUse,&MyType::prepareForUseCompleted, this),
          _prepareForShutdown( "prepareForShutdown", &MyType::prepareForShutdown,&MyType::prepareForShutdownCompleted, this),
          _addDriveOffset( "addDriveOffset", &MyType::addDriveOffset,&MyType::addDriveOffsetCompleted, this),
          _driveValue(KUKA361_NUM_AXES),
          _positionValue(KUKA361_NUM_AXES),
          _velocityValue(KUKA361_NUM_AXES), //oktober 2006
          _torqueValue(KUKA361_NUM_AXES), //oktober 2006
          _velocityLimits("velocityLimits","velocity limits of the axes, (rad/s)",vector<double>(KUKA361_NUM_AXES,0)),
          _currentLimits("currentLimits","current limits of the axes, (A)",vector<double>(KUKA361_NUM_AXES,0)), //oktober 2006
          _lowerPositionLimits("LowerPositionLimits","Lower position limits (rad)",vector<double>(KUKA361_NUM_AXES,0)),
          _upperPositionLimits("UpperPositionLimits","Upper position limits (rad)",vector<double>(KUKA361_NUM_AXES,0)),
          _initialPosition("initialPosition","Initial position (rad) for simulation or hardware",vector<double>(KUKA361_NUM_AXES,0)),
          _velDriveOffset("velDriveOffset","offset (in rad/s) to the drive value.",vector<double>(KUKA361_NUM_AXES,0)),
          _simulation("simulation","true if simulationAxes should be used",true),
          _num_axes("NUM_AXES",KUKA361_NUM_AXES),
          _velocityOutOfRange("velocityOutOfRange"),
          _currentOutOfRange("currentOutOfRange"), //oktober 2006
          _positionOutOfRange("positionOutOfRange"),
          _propertyfile(propertyfile),
          _activated(false),
          _positionConvertFactor(KUKA361_NUM_AXES),
          _driveConvertFactor(KUKA361_NUM_AXES),
          _tachoConvertScale(KUKA361_NUM_AXES), //oktober 2006
          _tachoConvertOffset(KUKA361_NUM_AXES), //oktober 2006
          _curReg_a(KUKA361_NUM_AXES), //oktober 2006
          _curReg_b(KUKA361_NUM_AXES), //oktober 2006
          _shunt_R(KUKA361_NUM_AXES), //oktober 2006
          _Km(KUKA361_NUM_AXES), //oktober 2006
          _torqueControlled(KUKA361_NUM_AXES), //oktober 2006
#if (defined (OROPKG_OS_LXRT))
          _axes_hardware(KUKA361_NUM_AXES),
          _encoderInterface(KUKA361_NUM_AXES),
          _encoder(KUKA361_NUM_AXES),
          _ref(KUKA361_NUM_AXES),
          _enable(KUKA361_NUM_AXES),
          _drive(KUKA361_NUM_AXES),
          _brake(KUKA361_NUM_AXES),
          _tachoInput(KUKA361_NUM_AXES), //oktober 2006
          _tachometer(KUKA361_NUM_AXES), //oktober 2006
          _currentInput(KUKA361_NUM_AXES), //oktober 2006
          _currentSensor(KUKA361_NUM_AXES), //oktober 2006
          _modeSwitch(KUKA361_NUM_AXES), //oktober 2006
          //_modeCheck, //oktober 2006
#endif
          _axes(KUKA361_NUM_AXES),
          _axes_simulation(KUKA361_NUM_AXES),
          pos_sim(KUKA361_NUM_AXES),//oktober 2006
          vel_sim(KUKA361_NUM_AXES),//oktober 2006
          tau_sim(KUKA361_NUM_AXES),//oktober 2006
          acc_sim(KUKA361_NUM_AXES)//oktober 2006
    {
        double ticks2rad[KUKA361_NUM_AXES] = KUKA361_TICKS2RAD;
        double vel2volt[KUKA361_NUM_AXES] = KUKA361_RADproSEC2VOLT;
        double tachoscale[KUKA361_NUM_AXES] = KUKA361_TACHOSCALE;//oktober 2006
        double tachooffset[KUKA361_NUM_AXES] = KUKA361_TACHOOFFSET;//oktober 2006
        double curReg_a[KUKA361_NUM_AXES] = KUKA361_A;//oktober 2006
        double curReg_b[KUKA361_NUM_AXES] = KUKA361_B;//oktober 2006
        double shunt_R[KUKA361_NUM_AXES] = KUKA361_R;//oktober 2006
        double KM[KUKA361_NUM_AXES] = KUKA361_KM;//oktober 2006
        double torquecontrolled[KUKA361_NUM_AXES] = TORQUE_CONTROLLED;//oktober 2006
        for(unsigned int i = 0;i<KUKA361_NUM_AXES;i++){
            _positionConvertFactor[i] = ticks2rad[i];
            _driveConvertFactor[i] = vel2volt[i];
            _tachoConvertScale[i] = tachoscale[i];//oktober 2006
            _tachoConvertOffset[i] = tachooffset[i];//oktober 2006
            _curReg_a[i] = curReg_a[i];//oktober 2006
            _curReg_b[i] = curReg_b[i];//oktober 2006
            _shunt_R[i] = shunt_R[i];//oktober 2006
            _Km[i] = KM[i];//oktober 2006
            _torqueControlled[i] = torquecontrolled[i];//oktober 2006
        }
		
        properties()->addProperty( &_velocityLimits );
        properties()->addProperty( &_currentLimits );
        properties()->addProperty( &_lowerPositionLimits );
        properties()->addProperty( &_upperPositionLimits  );
        properties()->addProperty( &_initialPosition  );
        properties()->addProperty( &_velDriveOffset  );
        properties()->addProperty( &_simulation  );
        attributes()->addConstant( &_num_axes);
    
        if (!marshalling()->readProperties(_propertyfile)) {
            log(Error) << "Failed to read the property file, continueing with default values." << endlog();
        }  
       
#if (defined (OROPKG_OS_LXRT))
        int encoderOffsets[KUKA361_NUM_AXES] = KUKA361_ENCODEROFFSETS;
        
        _comediDev        = new ComediDevice( 1 );
        _comediSubdevAOut = new ComediSubDeviceAOut( _comediDev, "Kuka361" );
        _apci1710         = new EncoderSSI_apci1710_board( 0, 1 );
        _apci2200         = new RelayCardapci2200( "Kuka361" );
        _apci1032         = new SwitchDigitalInapci1032( "Kuka361" );
        _comediDev_NI6024  = new ComediDevice( 4 ); //oktober 2006
        _comediSubdevAIn_NI6024  = new ComediSubDeviceAIn( _comediDev_NI6024, "Kuka361", 0 ); //oktober 2006
        _comediSubdevDIn_NI6024  = new ComediSubDeviceDIn( _comediDev_NI6024, "Kuka361", 2 ); //oktober 2006
        _comediDev_NI6527  = new ComediDevice( 3 ); //oktober 2006
        _comediSubdevDOut_NI6527  = new ComediSubDeviceDOut( _comediDev_NI6527, "Kuka361", 1 ); //oktober 2006
        
        
        for (unsigned int i = 0; i < KUKA361_NUM_AXES; i++){
            //Setting up encoders
            _encoderInterface[i] = new EncoderSSI_apci1710( i + 1, _apci1710 );
            _encoder[i]          = new AbsoluteEncoderSensor( _encoderInterface[i], 1.0 / ticks2rad[i], encoderOffsets[i], -10, 10 );

            _brake[i] = new DigitalOutput( _apci2200, i + KUKA361_NUM_AXES );
            _brake[i]->switchOn();

            _tachoInput[i] = new AnalogInput<unsigned int>(_comediSubdevAIn_NI6024, i+TACHO_OFFSET); //oktober 2006
            unsigned int range = 0; // The input range is -10 to 10 V, so range 0 //oktober 2006
            _comediSubdevAIn_NI6024->rangeSet(i+TACHO_OFFSET, range); //oktober 2006
            _comediSubdevAIn_NI6024->arefSet(i+TACHO_OFFSET, AnalogInInterface<unsigned int>::Common); //oktober 2006
            _tachometer[i] = new AnalogSensor( _tachoInput[i], _comediSubdevAIn_NI6024->lowest(i+TACHO_OFFSET), _comediSubdevAIn_NI6024->highest(i+TACHO_OFFSET), _tachoConvertScale[i], _tachoConvertOffset[i]); //oktober 2006

            _currentInput[i] = new AnalogInput<unsigned int>(_comediSubdevAIn_NI6024, i+CURRENT_OFFSET); //oktober 2006
            range = 1; // for a input range -5 to 5 V, range is 1 //oktober 2006
            _comediSubdevAIn_NI6024->rangeSet(i+CURRENT_OFFSET, range); //oktober 2006
            _comediSubdevAIn_NI6024->arefSet(i+CURRENT_OFFSET, AnalogInInterface<unsigned int>::Common); //oktober 2006
            _currentSensor[i] = new AnalogSensor( _currentInput[i], _comediSubdevAIn_NI6024->lowest(i+CURRENT_OFFSET), _comediSubdevAIn_NI6024->highest(i+CURRENT_OFFSET), 1.0 / _shunt_R[i], 0); //oktober 2006

            _modeSwitch[i] = new DigitalOutput( _comediSubdevDOut_NI6527, i+MODE_OFFSET ); //Velocity or torque control, selected by relay board //oktober 2006
//            _modeCheck[i] = new DigitalInput( _comediSubdevDIn_NI6024, i ); //oktober 2006

            _ref[i]   = new AnalogOutput<unsigned int>( _comediSubdevAOut, i );
            _enable[i] = new DigitalOutput( _apci2200, i );

            if ( _torqueControlled[i] ){
                     _modeSwitch[i]->switchOn(); //oktober 2006
//                     if ( !_modeCheck[i]->isOn() ) {
//                                log(Error) << "Failed to switch relay of channel " << i << " to torque control mode" << endlog(); //oktober 2006
//                     }
                     //_drive[i]  = new AnalogDrive( _ref[i], _enable[i], _curReg_a[i] / _shunt_R[i], - _curReg_b[i] / _shunt_R[i]); //oktober 2006
                     _drive[i]  = new AnalogDrive( _ref[i], _enable[i], 1.0 / _shunt_R[i], 0.0); //oktober 2006
            }
            else{
                     _drive[i]  = new AnalogDrive( _ref[i], _enable[i], 1.0 / vel2volt[i], _velDriveOffset.value()[i]);
            }
      
            _axes_hardware[i] = new RTT::Axis( _drive[i] );
            _axes_hardware[i]->setBrake( _brake[i] );
            _axes_hardware[i]->setSensor( "Position", _encoder[i] );
            _axes_hardware[i]->setSensor( "Velocity", _tachometer[i] ); //oktober 2006
            _axes_hardware[i]->setSensor( "Current", _currentSensor[i] ); //oktober 2006
//            _axes_hardware[i]->setSwitch( "Mode", _modeCheck[i] ); //oktober 2006

            if ( _torqueControlled[i] ){
                     _axes_hardware[i]->limitDrive(-_currentLimits.value()[i], _currentLimits.value()[i], _currentOutOfRange); //oktober 2006
            }else{
                     _axes_hardware[i]->limitDrive(-_velocityLimits.value()[i], _velocityLimits.value()[i], _velocityOutOfRange);
            }
        }
        
#endif
        for (unsigned int i = 0; i <KUKA361_NUM_AXES; i++){
             if ( _torqueControlled[i] ) {
                   _axes_simulation[i] = new RTT::TorqueSimulationAxis(_initialPosition.value()[i], _lowerPositionLimits.value()[i], _upperPositionLimits.value()[i],_velocityLimits.value()[i]);
            } else {
                   _axes_simulation[i] = new RTT::SimulationAxis(_initialPosition.value()[i], _lowerPositionLimits.value()[i], _upperPositionLimits.value()[i]);
             }
        }

#if (defined (OROPKG_OS_LXRT))
        if(!_simulation.value()){
            for (unsigned int i = 0; i <KUKA361_NUM_AXES; i++)
                _axes[i] = _axes_hardware[i];
            log(Info) << "LXRT version of LiASnAxesVelocityController has started" << endlog();
        }
        else{
            for (unsigned int i = 0; i <KUKA361_NUM_AXES; i++)
                _axes[i] = _axes_simulation[i];
            log(Info) << "LXRT simulation version of Kuka361nAxesTorqueController has started" << endlog();
        }
#else
        for (unsigned int i = 0; i <KUKA361_NUM_AXES; i++)
            _axes[i] = _axes_simulation[i];
        log(Info) << "GNULINUX simulation version of Kuka361nAxesTorqueController has started" << endlog();
#endif
        
        // make task context
        /*
         *  Command Interface
         */
        
        this->commands()->addCommand( &_startAxis, "start axis, starts updating the drive-value (only possible after unlockAxis)","axis","axis to start" );
        this->commands()->addCommand( &_stopAxis,"stop axis, sets drive value to zero and disables the update of the drive-port, (only possible if axis is started","axis","axis to stop");
        this->commands()->addCommand( &_lockAxis,"lock axis, enables the brakes (only possible if axis is stopped","axis","axis to lock" );
        this->commands()->addCommand( &_unlockAxis,"unlock axis, disables the brakes and enables the drive (only possible if axis is locked","axis","axis to unlock" );
        this->commands()->addCommand( &_startAllAxes, "start all axes"  );
        this->commands()->addCommand( &_stopAllAxes, "stops all axes"  );
        this->commands()->addCommand( &_lockAllAxes, "locks all axes"  );
        this->commands()->addCommand( &_unlockAllAxes, "unlock all axes"  );
        this->commands()->addCommand( &_prepareForUse, "prepares the robot for use"  );
        this->commands()->addCommand( &_prepareForShutdown,"prepares the robot for shutdown"  );
        this->commands()->addCommand( &_addDriveOffset,"adds an offset to the drive value of axis","axis","axis to add offset to","offset","offset value in rad/s" );


        /**
         * Creating and adding the data-ports
         */
        for (int i=0;i<KUKA361_NUM_AXES;++i) {
            char buf[80];
            sprintf(buf,"driveValue%d",i);
            _driveValue[i] = new ReadDataPort<double>(buf);
            ports()->addPort(_driveValue[i]);
            sprintf(buf,"positionValue%d",i);
            _positionValue[i]  = new DataPort<double>(buf);
            ports()->addPort(_positionValue[i]);
            sprintf(buf,"velocityValue%d",i); //oktober 2006
            _velocityValue[i]  = new DataPort<double>(buf); //oktober 2006
            ports()->addPort(_velocityValue[i]); //oktober 2006
            sprintf(buf,"torqueValue%d",i); //oktober 2006
            _torqueValue[i]  = new DataPort<double>(buf); //oktober 2006
            ports()->addPort(_torqueValue[i]); //oktober 2006
        }
        
    /**
     * Adding the events :
     */
    events()->addEvent( &_velocityOutOfRange, "Velocity of an Axis is out of range","message","Information about event" );
    events()->addEvent( &_positionOutOfRange, "Position of an Axis is out of range","message","Information about event");
    events()->addEvent( &_currentOutOfRange, "Current of an Axis is out of range","message","Information about event"); //oktober 2006
  }
    
    Kuka361nAxesTorqueController::~Kuka361nAxesTorqueController()
    {
        // make sure robot is shut down
        prepareForShutdown();
    
        // brake, drive, sensors and switches are deleted by each axis
        for (unsigned int i = 0; i < KUKA361_NUM_AXES; i++)
            delete _axes_simulation[i];
    
#if (defined (OROPKG_OS_LXRT))
        for (unsigned int i = 0; i < KUKA361_NUM_AXES; i++)
        delete _axes_hardware[i];
        delete _comediDev;
        delete _comediSubdevAOut;
        delete _apci1710;
        delete _apci2200;
        delete _apci1032;
        delete _comediDev_NI6024; //oktober 2006
        delete _comediSubdevAIn_NI6024; //oktober 2006
        delete _comediSubdevDIn_NI6024; //oktober 2006
        delete _comediDev_NI6527; //oktober 2006
        delete _comediSubdevDOut_NI6527; //oktober 2006
#endif
    }
  
  
    bool Kuka361nAxesTorqueController::startup()
    {
        return true;
    }
  
    void Kuka361nAxesTorqueController::update()
    {

#if (defined (OROPKG_OS_LXRT)) //oktober 2006
         if(_simulation.rvalue()) {
              for (int axis=0;axis<KUKA361_NUM_AXES;axis++) {
                    pos_sim[axis] = _axes[axis]->getSensor("Position")->readSensor();
                    vel_sim[axis] = _axes[axis]->getSensor("Velocity")->readSensor();
                    if ( _torqueControlled[axis] ) tau_sim[axis] = _driveValue[axis]->Get();
                    else tau_sim[axis] = 0;
              }
              acc_sim = kuka361DM.fwdyn361( tau_sim, vel_sim, pos_sim);
         }
#else
              for (int axis=0;axis<KUKA361_NUM_AXES;axis++) {
                    pos_sim[axis] = _axes[axis]->getSensor("Position")->readSensor();
                    vel_sim[axis] = _axes[axis]->getSensor("Velocity")->readSensor();
                    if ( _torqueControlled[axis] ) tau_sim[axis] = _driveValue[axis]->Get();
                    else tau_sim[axis] = 0;
              }
              acc_sim = kuka361DM.fwdyn361( tau_sim, vel_sim, pos_sim);
#endif

        for (int axis=0;axis<KUKA361_NUM_AXES;axis++) {
            // Set the position and perform checks in joint space.
            _positionValue[axis]->Set(_axes[axis]->getSensor("Position")->readSensor());

            // emit event when position is out of range
            if( (_positionValue[axis]->Get() < _lowerPositionLimits.value()[axis]) ||
                (_positionValue[axis]->Get() > _upperPositionLimits.value()[axis]) )
                _positionOutOfRange("Position  of a Kuka361 Axis is out of range");

            // Set the velocity and perform checks in joint space.
            _velocityValue[axis]->Set(_axes[axis]->getSensor("Velocity")->readSensor()); //oktober 2006

            // emit event when velocity is out of range
            if( (_velocityValue[axis]->Get() < -_velocityLimits.value()[axis]) ||
                (_velocityValue[axis]->Get() > _velocityLimits.value()[axis]) )
                _velocityOutOfRange("Velocity  of a Kuka361 Axis is out of range"); //oktober 2006

            if( _torqueControlled[axis] ){
//              _Km[axis] = _torqueValue[axis]->Get() * _Km[axis] / _axes[axis]->getDriveValue(); //Update Km= Im[k]*Km/Id[k-1] //oktober 2006
//              _torqueValue[axis]->Set(_axes[axis]->getSensor("Current")->readSensor() * _Km[axis]); // Set the measured torque //oktober 2006
		_torqueValue[axis]->Set(_axes[axis]->getSensor("Current")->readSensor());
            }

            // send the drive value to hw and performs checks, convert torque to current if torque controlled
            if (_axes[axis]->isDriven()) {
                 if( _torqueControlled[axis] ){
#if (defined (OROPKG_OS_LXRT))
        if(!_simulation.rvalue())
                       //_axes[axis]->drive(_driveValue[axis]->Get() / _Km[axis]); // accepts a current //oktober 2006
			_axes[axis]->drive(_driveValue[axis]->Get());
        else
                       //((TorqueSimulationAxis*) _axes[axis])->drive_sim(_driveValue[axis]->Get() / _Km[axis], acc_sim[axis]); //oktober 2006
                       ((TorqueSimulationAxis*) _axes[axis])->drive_sim(_driveValue[axis]->Get(), acc_sim[axis]); //oktober 2006
#else
                       //((TorqueSimulationAxis*) _axes[axis])->drive_sim(_driveValue[axis]->Get() / _Km[axis], acc_sim[axis]); //oktober 2006
                       ((TorqueSimulationAxis*) _axes[axis])->drive_sim(_driveValue[axis]->Get(), acc_sim[axis]); //oktober 2006
#endif
                 }
                 else
                      _axes[axis]->drive(_driveValue[axis]->Get());
            }
        }
	Logger::log()<<Logger::Debug<<"pos (rad): "<<_positionValue[3]->Get()<<" | vel (rad/s): "<<_velocityValue[3]->Get()<<" | drive (A): "<<_driveValue[3]->Get()<<" | cur (A): "<<_torqueValue[3]->Get()<<Logger::endl;
    }
    
    
    void Kuka361nAxesTorqueController::shutdown()
    {
        //Make sure machine is shut down
        prepareForShutdown();
        //Write properties back to file
        marshalling()->writeProperties(_propertyfile);
    }
  
    
    bool Kuka361nAxesTorqueController::prepareForUse()
    {
#if (defined (OROPKG_OS_LXRT))
        if(!_simulation.value()){
            _apci2200->switchOn( 12 );
            _apci2200->switchOn( 14 );
            Logger::log()<<Logger::Warning<<"Release Emergency stop and push button to start ...."<<Logger::endl;
        }
#endif
        _activated = true;
        return true;
    }
    
    bool Kuka361nAxesTorqueController::prepareForUseCompleted()const
    {
#if (defined (OROPKG_OS_LXRT))
        if(!_simulation.rvalue())
            return (_apci1032->isOn(12) && _apci1032->isOn(14));
        else
#endif
            return true;
    }
    
    bool Kuka361nAxesTorqueController::prepareForShutdown()
    {
        //make sure all axes are stopped and locked
        stopAllAxes();
        lockAllAxes();
#if (defined (OROPKG_OS_LXRT))
        if(!_simulation.value()){
            _apci2200->switchOff( 12 );
            _apci2200->switchOff( 14 );
        }
        
#endif
        _activated = false;
        return true;
    }
    
    bool Kuka361nAxesTorqueController::prepareForShutdownCompleted()const
    {
        return true;
    }
  
    bool Kuka361nAxesTorqueController::stopAxisCompleted(int axis)const
    {
        return _axes[axis]->isStopped();
    }
  
    bool Kuka361nAxesTorqueController::lockAxisCompleted(int axis)const
    {
        return _axes[axis]->isLocked();
    }
  
    bool Kuka361nAxesTorqueController::startAxisCompleted(int axis)const
    {
        return _axes[axis]->isDriven();
    }
  
    bool Kuka361nAxesTorqueController::unlockAxisCompleted(int axis)const
    {
        return !_axes[axis]->isLocked();
    }
  
    bool Kuka361nAxesTorqueController::stopAxis(int axis)
    {
        if (!(axis<0 || axis>KUKA361_NUM_AXES-1))
            return _axes[axis]->stop();
        else{
          Logger::log()<<Logger::Error<<"Axis "<< axis <<"doesn't exist!!"<<Logger::endl;
          return false;
        }
    }
    
    bool Kuka361nAxesTorqueController::startAxis(int axis)
    {
        if (!(axis<0 || axis>KUKA361_NUM_AXES-1))
#if (defined (OROPKG_OS_LXRT))
        if(!_simulation.rvalue())
            return _axes[axis]->drive(0.0);
        else
            return ((TorqueSimulationAxis*) _axes[axis])->drive_sim(0.0, 0.0);
#else
            return ((TorqueSimulationAxis*) _axes[axis])->drive_sim(0.0, 0.0);
#endif
        else{
            Logger::log()<<Logger::Error<<"Axis "<< axis <<"doesn't exist!!"<<Logger::endl;
            return false;
        }
    }
  
    bool Kuka361nAxesTorqueController::unlockAxis(int axis)
    {
        if(_activated){
            if (!(axis<0 || axis>KUKA361_NUM_AXES-1))
                return _axes[axis]->unlock();
            else{
                Logger::log()<<Logger::Error<<"Axis "<< axis <<"doesn't exist!!"<<Logger::endl;
                return false;
            }
        }
        else
            return false;
    }
  
    bool Kuka361nAxesTorqueController::lockAxis(int axis)
    {
        if (!(axis<0 || axis>KUKA361_NUM_AXES-1))
            return _axes[axis]->lock();
        else{
            Logger::log()<<Logger::Error<<"Axis "<< axis <<"doesn't exist!!"<<Logger::endl;
            return false;
        }
    }
  
    bool Kuka361nAxesTorqueController::stopAllAxes()
    {
        bool _return = true;
        for(unsigned int i = 0;i<KUKA361_NUM_AXES;i++){
            _return &= stopAxis(i);
        }
        return _return;
    }
  
    bool Kuka361nAxesTorqueController::startAllAxes()
    {
        bool _return = true;
        for(unsigned int i = 0;i<KUKA361_NUM_AXES;i++){
            _return &= startAxis(i);
        }
        return _return;
    }
  
    bool Kuka361nAxesTorqueController::unlockAllAxes()
    {
        bool _return = true;
        for(unsigned int i = 0;i<KUKA361_NUM_AXES;i++){
            _return &= unlockAxis(i);
        }
        return _return;
    }
  
    bool Kuka361nAxesTorqueController::lockAllAxes()
    {
        bool _return = true;
        for(unsigned int i = 0;i<KUKA361_NUM_AXES;i++){
            _return &= lockAxis(i);
        }
        return _return;
    }
  
    bool Kuka361nAxesTorqueController::stopAllAxesCompleted()const
    {
        bool _return = true;
        for(unsigned int i = 0;i<KUKA361_NUM_AXES;i++)
            _return &= stopAxisCompleted(i);
        return _return;
    }
  
    bool Kuka361nAxesTorqueController::startAllAxesCompleted()const
    {
        bool _return = true;
        for(unsigned int i = 0;i<KUKA361_NUM_AXES;i++)
            _return &= startAxisCompleted(i);
        return _return;
    }
  
    bool Kuka361nAxesTorqueController::lockAllAxesCompleted()const
    {
        bool _return = true;
        for(unsigned int i = 0;i<KUKA361_NUM_AXES;i++)
            _return &= lockAxisCompleted(i);
        return _return;
    }
  
    bool Kuka361nAxesTorqueController::unlockAllAxesCompleted()const
    {
        bool _return = true;
        for(unsigned int i = 0;i<KUKA361_NUM_AXES;i++)
            _return &= unlockAxisCompleted(i);
        return _return;
    }
  
    bool Kuka361nAxesTorqueController::addDriveOffset(int axis, double offset)
    {
        _velDriveOffset.value()[axis] += offset;
        
#if (defined (OROPKG_OS_LXRT))
        if (!_simulation.value())
            ((Axis*)(_axes[axis]))->getDrive()->addOffset(offset);
#endif
        return true;
    }
  
    bool Kuka361nAxesTorqueController::addDriveOffsetCompleted(int axis)const
    {
        return true;
    }
    
}//namespace orocos