/*
 * Centrald - RTS2 coordinator
 * Copyright (C) 2003-2008 Petr Kubanek <petr@kubanek.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include "centrald.h"
#include "../utils/rts2command.h"
#include "../utils/rts2centralstate.h"
#include "../utils/timestamp.h"

void
Rts2ConnCentrald::setState (int in_value)
{
	Rts2Conn::setState (in_value);
	// distribute weather updates..
	if (serverState->maskValueChanged (WEATHER_MASK))
	{
		master->weatherChanged ();
	}
	if (serverState->maskValueChanged (BOP_MASK))
	{
		master->bopMaskChanged ();
	}
}


Rts2ConnCentrald::Rts2ConnCentrald (int in_sock, Rts2Centrald * in_master,
int in_centrald_id):
Rts2Conn (in_sock, in_master)
{
	master = in_master;
	setCentraldId (in_centrald_id);
	messageMask = 0x00;

	statusCommandRunning = 0;
}


Rts2ConnCentrald::~Rts2ConnCentrald (void)
{
	setPriority (-1);
	master->changePriority (0);
}


int
Rts2ConnCentrald::priorityCommand ()
{
	int timeout;
	int new_priority;

	if (isCommand ("priority"))
	{
		if (paramNextInteger (&new_priority) || !paramEnd ())
			return -1;
		timeout = 0;
	}
	else
	{
		if (paramNextInteger (&new_priority) ||
			paramNextInteger (&timeout) || !paramEnd ())
			return -1;
		timeout += time (NULL);
	}

	setPriority (new_priority);

	if (master->changePriority (timeout))
	{
		sendCommandEnd (DEVDEM_E_PRIORITY, "error when processing priority request");
		return -1;
	}

	return 0;
}


int
Rts2ConnCentrald::sendDeviceKey ()
{
	int dev_key;
	dev_key = random ();
	if (dev_key == 0)
		dev_key = 1;
	std::ostringstream _os;	
	// device number could change..device names don't
	char *dev_name;
	Rts2Conn *dev;
	if (paramNextString (&dev_name) || !paramEnd ())
		return -2;
	// find device, set it authorization key
	dev = master->findName (dev_name);
	if (!dev)
	{
		sendCommandEnd (DEVDEM_E_SYSTEM, "cannot find device with name");
		return -1;
	}
	setKey (dev_key);
	_os << "authorization_key " << dev_name << " " << getKey ();
	sendMsg (_os);
	return 0;
}


int
Rts2ConnCentrald::sendMessage (Rts2Message & msg)
{
	if (msg.passMask (messageMask))
		return Rts2Conn::sendMessage (msg);
	return -1;
}


int
Rts2ConnCentrald::sendInfo ()
{
	if (!paramEnd ())
		return -2;

	connections_t::iterator iter;
	for (iter = master->getConnections ()->begin (); iter != master->getConnections ()->end (); iter++)
	{
		Rts2ConnCentrald *conn = (Rts2ConnCentrald *) * iter;
		conn->sendConnectedInfo (this);
	}
	((Rts2Centrald *)getMaster ())->sendInfo (this);
	return 0;
}


int
Rts2ConnCentrald::sendConnectedInfo (Rts2Conn * conn)
{
	std::ostringstream _os;
	int ret = -1;

	switch (getType ())
	{
		case CLIENT_SERVER:
			_os << "user "
				<< getCentraldId () << " "
				<< getPriority () << " "
				<< (havePriority ()? '*' : '-') << " "
				<< login;
			ret = conn->sendMsg (_os);
			break;
		case DEVICE_SERVER:
			_os << "device "
				<< getCentraldNum () << " "
				<< getCentraldId () << " "
				<< getName () << " "
				<< hostname << " "
				<< port << " "
				<< device_type;
			ret = conn->sendMsg (_os);
			break;
		default:
			break;
	}
	return ret;
}


void
Rts2ConnCentrald::updateStatusWait (Rts2Conn * conn)
{
	if (conn)
	{
		if (getMaster ()->commandOriginatorPending (this, conn))
			return;
	}
	else
	{
		if (statusCommandRunning == 0)
			return;
		if (getMaster ()->commandOriginatorPending (this, NULL))
			return;
	}

	master->sendStatusMessage (master->getState (), this);
	master->sendBopMessage (master->getStateForConnection (this), this);
	sendCommandEnd (DEVDEM_OK, "OK");
	statusCommandRunning--;
}


int
Rts2ConnCentrald::commandDevice ()
{
	if (isCommand ("authorize"))
	{
		int client;
		int dev_key;
		if (paramNextInteger (&client)
			|| paramNextInteger (&dev_key) || !paramEnd ())
			return -2;

		if (client < 0)
		{
			logStream (MESSAGE_ERROR) << "invalid client ID requested for authentification: " << client << " from " << getName () << sendLog;
			return -2;
		}

		Rts2Conn *conn = master->getConnection (client);

		// client vanished when we processed data..
		if (conn == NULL)
		{
			sendCommandEnd (DEVDEM_E_SYSTEM,
				"client vanished during auth sequence");
			return -1;
		}

		if (conn->getKey () == 0)
		{
			sendAValue ("authorization_failed", client);
			sendCommandEnd (DEVDEM_E_SYSTEM, "client didn't ask for authorization");
			return -1;
		}

		if (conn->getKey () != dev_key)
		{
			sendAValue ("authorization_failed", client);
			sendCommandEnd (DEVDEM_E_SYSTEM, "invalid authorization key");
			return -1;
		}

		sendAValue ("authorization_ok", client);
		sendInfo ();

		return 0;
	}
	if (isCommand ("key"))
	{
		return sendDeviceKey ();
	}
	if (isCommand ("info"))
	{
		master->info ();
		return sendInfo ();
	}
	if (isCommand ("on"))
	{
		return master->changeStateOn (getName ());
	}
	if (isCommand ("priority") || isCommand ("prioritydeferred"))
	{
		return priorityCommand ();
	}
	if (isCommand ("standby"))
	{
		return master->changeStateStandby (getName ());
	}
	if (isCommand ("off"))
	{
		return master->changeStateHardOff (getName ());
	}
	if (isCommand ("soft_off"))
	{
		return master->changeStateSoftOff (getName ());
	}
	return Rts2Conn::command ();
}


int
Rts2ConnCentrald::sendStatusInfo ()
{
	std::ostringstream _os;
	_os << PROTO_STATUS " " << master->getState ();
	return sendMsg (_os);
}


int
Rts2ConnCentrald::sendAValue (const char *val_name, int value)
{
	std::ostringstream _os;
	_os << PROTO_AUTH " " << val_name << " " << value;
	return sendMsg (_os);
}


int
Rts2ConnCentrald::commandClient ()
{
	if (isCommand ("password"))
	{
		char *passwd;
		if (paramNextString (&passwd) || !paramEnd ())
			return -2;

		if (strncmp (passwd, login, CLIENT_LOGIN_SIZE) == 0)
		{
			std::ostringstream _os;
			authorized = 1;
			_os << "logged_as " << getCentraldId ();
			sendMsg (_os);
			sendStatusInfo ();
			return 0;
		}
		else
		{
			sleep (5);			 // wait some time to prevent repeat attack
			sendCommandEnd (DEVDEM_E_SYSTEM, "invalid login or password");
			return -1;
		}
	}
	if (authorized)
	{
		if (isCommand ("ready"))
		{
			return 0;
		}

		if (isCommand ("info"))
		{
			master->info ();
			return sendInfo ();
		}
		if (isCommand ("priority") || isCommand ("prioritydeferred"))
		{
			return priorityCommand ();
		}
		if (isCommand ("key"))
		{
			return sendDeviceKey ();
		}
		if (isCommand ("on"))
		{
			return master->changeStateOn (login);
		}
		if (isCommand ("standby"))
		{
			return master->changeStateStandby (login);
		}
		if (isCommand ("off"))
		{
			return master->changeStateHardOff (login);
		}
		if (isCommand ("soft_off"))
		{
			return master->changeStateSoftOff (login);
		}
	}
	return Rts2Conn::command ();
}


int
Rts2ConnCentrald::command ()
{
	if (isCommand ("login"))
	{
		if (getType () == NOT_DEFINED_SERVER)
		{
			char *in_login;

			srandom (time (NULL));

			if (paramNextString (&in_login) || !paramEnd ())
				return -2;

			strncpy (login, in_login, CLIENT_LOGIN_SIZE);

			setType (CLIENT_SERVER);
			master->connAdded (this);
			return 0;
		}
		else
		{
			sendCommandEnd (DEVDEM_E_COMMAND,
				"cannot switch server type to CLIENT_SERVER");
			return -1;
		}
	}
	else if (isCommand ("register"))
	{
		if (getType () == NOT_DEFINED_SERVER)
		{
			int centraldNum;
			char *reg_device;
			char *in_hostname;
			std::ostringstream _os;

			if (paramNextInteger (&centraldNum)
				|| paramNextString (&reg_device)
				|| paramNextInteger (&device_type)
				|| paramNextString (&in_hostname)
				|| paramNextInteger (&port)
				|| !paramEnd ())
				return -2;

			if (master->findName (reg_device))
			{
				sendCommandEnd (DEVDEM_E_SYSTEM, "name already registered");
				return -1;
			}

			setName (centraldNum, reg_device);
			strncpy (hostname, in_hostname, HOST_NAME_MAX);

			setType (DEVICE_SERVER);
			sendStatusInfo ();
			if (master->getPriorityClient () > -1)
			{
				_os << PROTO_PRIORITY " " << master->getPriorityClient () << " 0";
				sendMsg (_os);
			}

			sendAValue ("registered_as", getCentraldId ());
			master->connAdded (this);
			sendInfo ();
			return 0;
		}
		else
		{
			sendCommandEnd (DEVDEM_E_COMMAND,
				"cannot switch server type to DEVICE_SERVER");
			return -1;
		}
	}
	else if (isCommand ("message_mask"))
	{
		int newMask;
		if (paramNextInteger (&newMask) || !paramEnd ())
			return -2;
		messageMask = newMask;
		return 0;
	}
	else if (getType () == DEVICE_SERVER)
		return commandDevice ();
	else if (getType () == CLIENT_SERVER)
		return commandClient ();
	// else
	return Rts2Conn::command ();
}


Rts2Centrald::Rts2Centrald (int argc, char **argv)
:Rts2Daemon (argc, argv, SERVERD_HARD_OFF | BAD_WEATHER)
{
	connNum = 0;

	configFile = NULL;
	logFileSource = LOGFILE_DEF;
	fileLog = NULL;

	priority_client = -1;

	createValue (morning_off, "morning_off", "switch to off at the morning", false);
	createValue (morning_standby, "morning_standby", "switch to standby at the morning", false);

	createValue (requiredDevices, "required_devices", "devices necessary to automatically switch system to on state", false);
	createValue (failedDevices, "failed_devices", "devices which are required but not present in the system", false);

	createValue (priorityClient, "priority_client", "client which have priority", false);
	createValue (priority, "priority", "current priority level", false);

	createValue (nextStateChange, "next_state_change", "time of next state change", false);
	createValue (nextState, "next_state", "next server state", false);
	nextState->addSelVal ("day");
	nextState->addSelVal ("evening");
	nextState->addSelVal ("dusk");
	nextState->addSelVal ("night");
	nextState->addSelVal ("dawn");
	nextState->addSelVal ("morning");

	createConstValue (observerLng, "longitude", "observatory longitude", false,
		RTS2_DT_DEGREES);
	createConstValue (observerLat, "latitude", "observatory latitude", false,
		RTS2_DT_DEC);

	createConstValue (nightHorizon, "night_horizon",
		"observatory night horizon", false, RTS2_DT_DEC);
	createConstValue (dayHorizon, "day_horizon", "observatory day horizon",
		false, RTS2_DT_DEC);

	createConstValue (eveningTime, "evening_time", "time needed to cool down cameras", false);
	createConstValue (morningTime, "morning_time", "time needed to heat up cameras", false);

	addOption (OPT_CONFIG, "config", 1, "configuration file");
	addOption (OPT_LOGFILE, "logfile", 1, "log file (put '-' to log to stderr");
}


Rts2Centrald::~Rts2Centrald (void)
{
	if (fileLog)
	{
		fileLog->close ();
		delete fileLog;
	}
	// do not report any priority changes
	priority_client = -2;
}


void
Rts2Centrald::openLog ()
{
	if (fileLog)
	{
		fileLog->close ();
		delete fileLog;
	}
	if (logFile == std::string ("-"))
	{
		fileLog = NULL;
		return;
	}
	fileLog = new std::ofstream ();
	fileLog->open (logFile.c_str (), std::ios_base::out | std::ios_base::app);
}


int
Rts2Centrald::reloadConfig ()
{
	int ret;
	Rts2Config *config = Rts2Config::instance ();
	ret = config->loadFile (configFile);
	if (ret)
		return ret;

	if (logFileSource != LOGFILE_ARG)
	{
		config->getString ("centrald", "logfile", logFile, "/var/log/rts2-debug");
		logFileSource = LOGFILE_CNF;
	}

	openLog ();

	observer = config->getObserver ();

	observerLng->setValueDouble (observer->lng);
	observerLat->setValueDouble (observer->lat);

	requiredDevices->setValueArray (config->observatoryRequiredDevices ());

	double t_h;
	config->getDouble ("observatory", "night_horizon", t_h, -10);
	nightHorizon->setValueDouble (t_h);

	config->getDouble ("observatory", "day_horizon", t_h, 0);
	dayHorizon->setValueDouble (t_h);

	int t_t;
	config->getInteger ("observatory", "evening_time", t_t, 7200);
	eveningTime->setValueInteger (t_t);

	config->getInteger ("observatory", "morning_time", t_t, 1800);
	morningTime->setValueInteger (t_t);

	next_event_time = 0;

	constInfoAll ();

	return 0;
}


int
Rts2Centrald::processOption (int in_opt)
{
	switch (in_opt)
	{
		case OPT_CONFIG:
			configFile = optarg;
			break;
		case OPT_LOGFILE:
			logFile = std::string (optarg);
			logFileSource = LOGFILE_ARG;
			break;
		default:
			return Rts2Daemon::processOption (in_opt);
	}
	return 0;
}


int
Rts2Centrald::init ()
{
	int ret;
	setPort (atoi (CENTRALD_PORT));
	ret = Rts2Daemon::init ();
	if (ret)
		return ret;

	srandom (time (NULL));

	ret = reloadConfig ();
	if (ret)
		return ret;

	// only set morning_off and morning_standby values at firts config load
	morning_off->setValueBool (Rts2Config::instance ()->getBoolean ("centrald", "morning_off", true));
	morning_standby->setValueBool (Rts2Config::instance ()->getBoolean ("centrald", "morning_standby", true));

	centraldConnRunning (NULL);

	std::ostringstream _os;
	_os << getLockPrefix () << "centrald_" << getPort ();
	ret = checkLockFile (_os.str ().c_str ());
	if (ret)
		return ret;
	ret = doDeamonize ();
	if (ret)
		return ret;
	return lockFile ();
}


int
Rts2Centrald::initValues ()
{
	time_t curr_time;

	int call_state;

	curr_time = time (NULL);

	next_event (observer, &curr_time, &call_state, &next_event_type,
		&next_event_time, nightHorizon->getValueDouble (),
		dayHorizon->getValueDouble (), eveningTime->getValueInteger (),
		morningTime->getValueInteger ());

	Rts2Config *config = Rts2Config::instance ();

	if (config->getBoolean ("centrald", "reboot_on", false))
	{
		maskState (SERVERD_STATUS_MASK, call_state, "switched on centrald reboot");
	}
	else
	{
		maskState (SERVERD_STATUS_MASK, SERVERD_HARD_OFF, "switched on centrald reboot");
	}

	nextStateChange->setValueTime (next_event_time);
	nextState->setValueInteger (next_event_type);

	return Rts2Daemon::initValues ();
}


int
Rts2Centrald::setValue (Rts2Value *old_value, Rts2Value *new_value)
{
	if (old_value == morning_off || old_value == morning_standby)
		return 0;
	return Rts2Daemon::setValue (old_value, new_value);
}


void
Rts2Centrald::connectionRemoved (Rts2Conn * conn)
{
	// update weather
	weatherChanged ();
	// make sure we will change BOP mask..
	bopMaskChanged ();
	connections_t::iterator iter;
	// and make sure we aren't the last who block status info
	for (iter = getConnections ()->begin (); iter != getConnections ()->end (); iter++)
	{
		(*iter)->updateStatusWait (NULL);
	}
}


void
Rts2Centrald::stateChanged (int new_state, int old_state, const char *description)
{
	Rts2Daemon::stateChanged (new_state, old_state, description);
	if ((getState () & ~BOP_MASK) != (old_state & ~BOP_MASK))
	{
		logStream (MESSAGE_INFO) << "State changed from " << Rts2CentralState::getString (old_state)
			<< " to " << Rts2CentralState::getString (getState ())
			<< " description " << description
			<< sendLog;
		sendStatusMessage (getState ());
	}
}


Rts2Conn *
Rts2Centrald::createConnection (int in_sock)
{
	connNum++;
	return new Rts2ConnCentrald (in_sock, this, connNum);
}


void
Rts2Centrald::connAdded (Rts2ConnCentrald * added)
{
	connections_t::iterator iter;
	for (iter = getConnections ()->begin (); iter != getConnections ()->end (); iter++)
	{
		Rts2Conn *conn = *iter;
		added->sendConnectedInfo (conn);
	}
}


Rts2Conn *
Rts2Centrald::getConnection (int conn_num)
{
	connections_t::iterator iter;
	for (iter = getConnections ()->begin (); iter != getConnections ()->end (); iter++)
	{
		Rts2ConnCentrald *conn = (Rts2ConnCentrald *) * iter;
		if (conn->getCentraldId () == conn_num)
			return conn;
	}
	return NULL;
}


int
Rts2Centrald::changeState (int new_state, const char *user)
{
	logStream (MESSAGE_INFO) << "State switched to " << Rts2CentralState::getString (new_state) << " by " <<
		user << sendLog;
	maskState (SERVERD_STANDBY_MASK | SERVERD_STATUS_MASK, new_state, user);
	return 0;
}


int
Rts2Centrald::changePriority (time_t timeout)
{
	// do not perform any priority changes
	if (priority_client == -2)
		return 0;

	int new_priority_client = -1;
	int new_priority_max = 0;
	connections_t::iterator iter;
	Rts2Conn *conn = getConnection (priority_client);

								 // old priority client
	if (priority_client >= 0 && conn)
	{
		new_priority_client = priority_client;
		new_priority_max = conn->getPriority ();
	}
	// find new client with highest priority
	for (iter = getConnections ()->begin (); iter != getConnections ()->end (); iter++)
	{
		conn = *iter;
		if (conn->getPriority () > new_priority_max)
		{
			new_priority_client = conn->getCentraldId ();
			new_priority_max = conn->getPriority ();
		}
	}

	if (priority_client != new_priority_client)
	{
		conn = getConnection (priority_client);
		if (priority_client >= 0 && conn)
			conn->setHavePriority (0);

		priority_client = new_priority_client;

		conn = getConnection (priority_client);
		if (priority_client >= 0 && conn)
			conn->setHavePriority (1);
	}
	// send out priority change
	std::ostringstream _os;
	_os << PROTO_PRIORITY " " << priority_client << " " << timeout;
	sendAll (_os);

	// and set and send new priority values
	priorityClient->setValueCharArr (conn ? conn->getName () : "(null)");
	priority->setValueInteger (new_priority_max);

	sendValueAll (priorityClient);
	sendValueAll (priority);

	return 0;
}


int
Rts2Centrald::idle ()
{
	time_t curr_time;

	int call_state;
	int old_current_state;

	curr_time = time (NULL);

	if (curr_time < next_event_time)
		return Rts2Daemon::idle ();

	next_event (observer, &curr_time, &call_state, &next_event_type,
		&next_event_time, nightHorizon->getValueDouble (),
		dayHorizon->getValueDouble (), eveningTime->getValueInteger (),
		morningTime->getValueInteger ());

	if (getState () != call_state)
	{
		nextStateChange->setValueTime (next_event_time);
		nextState->setValueInteger (next_event_type);

		// update state only if state isn't OFF or SOFT_OFF
		if ((getState () & SERVERD_STATUS_MASK) != SERVERD_HARD_OFF
			&& (getState () & SERVERD_STATUS_MASK) != SERVERD_SOFT_OFF)
		{
			old_current_state = getState ();
			if ((getState () & SERVERD_STATUS_MASK) == SERVERD_MORNING
				&& (call_state & SERVERD_STATUS_MASK) == SERVERD_DAY)
			{
				if (morning_off->getValueBool ())
					maskState (SERVERD_STANDBY_MASK | SERVERD_STATUS_MASK, SERVERD_HARD_OFF, "by idle routine");
				else if (morning_standby->getValueBool ())
					maskState (SERVERD_STANDBY_MASK, SERVERD_STANDBY, "by idle routine");
				else
					maskState (SERVERD_STATUS_MASK, call_state, "by idle routine");
			}
			else
			{
				maskState (SERVERD_STATUS_MASK, call_state, "by idle routine");
			}
		}

		// send update about next state transits..
		infoAll ();
	}
	return Rts2Daemon::idle ();
}


void
Rts2Centrald::deviceReady (Rts2Conn * conn)
{
	Rts2Daemon::deviceReady (conn);
	// check again for weather state..
	weatherChanged ();
}


void
Rts2Centrald::sendMessage (messageType_t in_messageType, const char *in_messageString)
{
	Rts2Message msg = Rts2Message ("centrald", in_messageType, in_messageString);
	Rts2Daemon::sendMessage (in_messageType, in_messageString);
	processMessage (msg);
}


void
Rts2Centrald::message (Rts2Message & msg)
{
	processMessage (msg);
}


void
Rts2Centrald::processMessage (Rts2Message & msg)
{
	// log it
	if (fileLog)
	{
		(*fileLog) << msg;
	}
	else
	{
		std::cerr << msg.toString () << std::endl;
	}

	// and send it to all
	sendMessageAll (msg);
}


void
Rts2Centrald::signaledHUP ()
{
	reloadConfig ();
	Rts2Daemon::signaledHUP ();
}


void
Rts2Centrald::weatherChanged ()
{
	// state of the required devices
	std::vector <std::string> failedArr;
	std::vector <std::string>::iterator namIter;
	for (namIter = requiredDevices->valueBegin (); namIter != requiredDevices->valueEnd (); namIter++)
		failedArr.push_back (*namIter);

	connections_t::iterator iter;
	// check if some connection block weather
	for (iter = getConnections ()->begin (); iter != getConnections ()->end (); iter ++)
	{
		// if connection is required..
		namIter = std::find (failedArr.begin (), failedArr.end (), std::string ((*iter)->getName ()));
		if (namIter != failedArr.end ())
		{
			if ((*iter)->isGoodWeather () == true && (*iter)->isConnState (CONN_CONNECTED))
				failedArr.erase (namIter);
			// otherwise, connection name will not be erased from
			// failedArr, so failedArr size will be larger then 0,
			// so newWeather will be set to false in size check - few lines
			// bellow.
		}
		else  if ((*iter)->isGoodWeather () == false)
		{
			failedArr.push_back ((*iter)->getName ());
		}

	}
	failedDevices->setValueArray (failedArr);
	sendValueAll (failedDevices);

	setWeatherState (failedArr.size () > 0 ? false : true);
	if (failedArr.size () > 0)
	{
		Rts2LogStream ls = logStream (MESSAGE_DEBUG);
		ls << "failed devices:";
		for (namIter = failedArr.begin (); namIter != failedArr.end (); namIter++)
			ls << " " << (*namIter);
		ls << sendLog;
	}
}


void
Rts2Centrald::bopMaskChanged ()
{
	int bopState = 0;
	connections_t::iterator iter;
	for (iter = getConnections ()->begin (); iter != getConnections ()->end (); iter++)
	{
		bopState |= (*iter)->getBopState ();
		if ((*iter)->getType () == DEVICE_SERVER)
			sendBopMessage (getStateForConnection (*iter), *iter);
	}
	maskState (BOP_MASK, bopState, "changed BOP state");
	sendStatusMessage (getState ());
}


int
Rts2Centrald::statusInfo (Rts2Conn * conn)
{
	Rts2ConnCentrald *c_conn = (Rts2ConnCentrald *) conn;
	int s_count = 0;
	// update system status
	connections_t::iterator iter;
	for (iter = getConnections ()->begin (); iter != getConnections ()->end (); iter++)
	{
		Rts2ConnCentrald *test_conn = (Rts2ConnCentrald *) * iter;
		// do not request status from client connections
		if (test_conn != conn && test_conn->getType () != CLIENT_SERVER)
		{
			if (conn->getType () == DEVICE_SERVER)
			{
				if (Rts2Config::instance ()->blockDevice (conn->getName (), test_conn->getName ()) == false)
					continue;
			}
			Rts2CommandStatusInfo *cs = new Rts2CommandStatusInfo (this, c_conn);
			cs->setOriginator (conn);
			test_conn->queCommand (cs);
			s_count++;
		}
	}
	// command was not send at all..
	if (s_count == 0)
	{
		return 0;
	}

	c_conn->statusCommandSend ();

	// indicate command pending, we will send command end once we will get reply from all devices
	return -1;
}


int
Rts2Centrald::getStateForConnection (Rts2Conn * conn)
{
	if (conn->getType () != DEVICE_SERVER)
		return getState ();
	int sta = getState ();
	// get rid of BOP mask
	sta &= ~BOP_MASK & ~DEVICE_ERROR_MASK;
	connections_t::iterator iter;
	// cretae BOP mask for device
	for (iter = getConnections ()->begin (); iter != getConnections ()->end (); iter++)
	{
		Rts2Conn *test_conn = *iter;
		if (Rts2Config::instance ()->blockDevice (conn->getName (), test_conn->getName ()) == false)
			continue;
		sta |= test_conn->getBopState ();
	}
	return sta;
}


int
main (int argc, char **argv)
{
	Rts2Centrald centrald = Rts2Centrald (argc, argv);
	return centrald.run ();
}
