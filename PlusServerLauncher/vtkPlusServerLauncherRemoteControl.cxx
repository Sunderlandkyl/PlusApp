
#include <vtkPlusServerLauncherRemoteControl.h>

#include <vtkObjectFactory.h>

#include <QPlusDeviceSetSelectorWidget.h>
#include <vtkPlusLogger.h>

const char* PLUS_SERVER_LAUNCHER_REMOTE_DEVICE_ID = "PSL_Remote";

vtkStandardNewMacro(vtkPlusServerLauncherRemoteControl);

vtkPlusServerLauncherRemoteControl::vtkPlusServerLauncherRemoteControl()
{
}

//----------------------------------------------------------------------------
vtkPlusServerLauncherRemoteControl::~vtkPlusServerLauncherRemoteControl()
{
  if (this->RemoteControlServerLogic)
  {
    this->RemoteControlServerLogic->RemoveObserver(this->RemoteControlServerCallbackCommand);
  }

  if (vtkPlusLogger::Instance())
  {
    vtkPlusLogger::Instance()->RemoveObserver(this->LogMessageCallbackCommand);
  }

  // Wait for remote control thread to terminate
  this->RemoteControlActive.first = false;
  while (this->RemoteControlActive.second)
  {
    vtkPlusAccurateTimer::DelayWithEventProcessing(0.2);
  }
}

//---------------------------------------------------------------------------
PlusStatus vtkPlusServerLauncherRemoteControl::StartRemoteControlServer()
{
  LOG_INFO("Start remote control server at port: " << this->ServerPort);

  this->RemoteControlServerCallbackCommand = vtkSmartPointer<vtkCallbackCommand>::New();
  this->RemoteControlServerCallbackCommand->SetCallback(vtkPlusServerLauncherRemoteControl::OnRemoteControlServerEventReceived);
  this->RemoteControlServerCallbackCommand->SetClientData(this);

  this->LogMessageCallbackCommand = vtkSmartPointer<vtkCallbackCommand>::New();
  this->LogMessageCallbackCommand->SetCallback(vtkPlusServerLauncherRemoteControl::OnLogEvent);
  this->LogMessageCallbackCommand->SetClientData(this);

  this->RemoteControlServerLogic = igtlio::LogicPointer::New();
  this->RemoteControlServerSession = this->RemoteControlServerLogic->StartServer(this->ServerPort);
  this->RemoteControlServerConnector = this->RemoteControlServerSession->GetConnector();

  this->RemoteControlServerLogic->AddObserver(igtlio::Logic::CommandReceivedEvent, this->RemoteControlServerCallbackCommand);
  this->RemoteControlServerLogic->AddObserver(igtlio::Logic::CommandResponseReceivedEvent, this->RemoteControlServerCallbackCommand);
  this->RemoteControlServerConnector->AddObserver(igtlio::Connector::ConnectedEvent, this->RemoteControlServerCallbackCommand);
  this->RemoteControlServerConnector->AddObserver(igtlio::Connector::DisconnectedEvent, this->RemoteControlServerCallbackCommand);
  vtkPlusLogger::Instance()->AddObserver(vtkCommand::UserEvent, this->LogMessageCallbackCommand);

  // Create thread to receive commands
  this->Threader = vtkSmartPointer<vtkMultiThreader>::New();
  this->RemoteControlActive = std::make_pair(false, false);
  this->RemoteControlActive.first = true;
  this->Threader->SpawnThread((vtkThreadFunctionType)&PlusRemoteThread, this);

  return PLUS_SUCCESS;
}


//---------------------------------------------------------------------------
void* vtkPlusServerLauncherRemoteControl::PlusRemoteThread(vtkMultiThreader::ThreadInfo* data)
{
  vtkPlusServerLauncherRemoteControl* self = (vtkPlusServerLauncherRemoteControl*)(data->UserData);
  LOG_INFO("Remote control started");

  self->RemoteControlActive.second = true;
  while (self->RemoteControlActive.first)
  {
    self->RemoteControlServerLogic->PeriodicProcess();
    vtkPlusAccurateTimer::DelayWithEventProcessing(0.2);
  }
  self->RemoteControlActive.second = false;
  LOG_INFO("Remote control stopped");

  return NULL;
}

//---------------------------------------------------------------------------
PlusStatus vtkPlusServerLauncherRemoteControl::StopRemoteControlServer()
{
  return PLUS_SUCCESS;
}

//---------------------------------------------------------------------------
void vtkPlusServerLauncherRemoteControl::OnRemoteControlServerEventReceived(vtkObject* caller, unsigned long eventId, void* clientData, void* callData)
{
  vtkPlusServerLauncherRemoteControl* self = reinterpret_cast<vtkPlusServerLauncherRemoteControl*>(clientData);

  igtlio::LogicPointer logic = dynamic_cast<igtlio::Logic*>(caller);
  igtlio::ConnectorPointer connector = dynamic_cast<igtlio::Connector*>(caller);

  switch (eventId)
  {
  case igtlio::Connector::ConnectedEvent:
    vtkPlusServerLauncherRemoteControl::OnConnectEvent(self, connector);
    break;
  case igtlio::Connector::DisconnectedEvent:
    vtkPlusServerLauncherRemoteControl::OnDisconnectEvent(self, connector);
    break;
  case igtlio::Logic::CommandReceivedEvent:
    vtkPlusServerLauncherRemoteControl::OnCommandReceivedEvent(self, logic);
    break;
  case igtlio::Logic::CommandResponseReceivedEvent:
    break;
  default:
    break;
  }

}

//---------------------------------------------------------------------------
void vtkPlusServerLauncherRemoteControl::OnConnectEvent(vtkPlusServerLauncherRemoteControl* self, igtlio::ConnectorPointer connector)
{
  LOG_INFO("Launcher remote control connected.")
}

//---------------------------------------------------------------------------
void vtkPlusServerLauncherRemoteControl::OnDisconnectEvent(vtkPlusServerLauncherRemoteControl* self, igtlio::ConnectorPointer connector)
{
  LOG_INFO("Launcher remote control disconnected.")
}

//---------------------------------------------------------------------------
void vtkPlusServerLauncherRemoteControl::OnCommandReceivedEvent(vtkPlusServerLauncherRemoteControl* self, igtlio::LogicPointer logic)
{
  if (!logic)
  {
    LOG_ERROR("Command event could not be read!");
  }

  for (unsigned int i = 0; i < logic->GetNumberOfDevices(); ++i)
  {
    igtlio::DevicePointer device = logic->GetDevice(i);
    if (STRCASECMP(device->GetDeviceType().c_str(), "COMMAND") == 0)
    {
      igtlio::CommandDevicePointer commandDevice = igtlio::CommandDevice::SafeDownCast(device);
      if (commandDevice && commandDevice->MessageDirectionIsIn())
      {
        igtlio::CommandConverter::ContentData contentData = commandDevice->GetContent();
        std::string commandNameString = contentData.name;
        const char* commandName = commandNameString.c_str();
        std::string content = contentData.content;

        vtkSmartPointer<vtkXMLDataElement> rootElement = vtkSmartPointer<vtkXMLDataElement>::Take(vtkXMLUtilities::ReadElementFromString(content.c_str()));
        if (!rootElement)
        {
          LOG_ERROR("ParseCommand: Error parsing xml");
          continue;
        }

        if (STRCASECMP(commandName, "StartServer") == 0)
        {
          self->StartServerCommand(self, rootElement, commandDevice);
        }
        else if (STRCASECMP(commandName, "StopServer") == 0)
        {
          self->StopServerCommand(self, rootElement, commandDevice);
        }
        else if (STRCASECMP(commandName, "GetServerInfo") == 0)
        {
          self->GetServerInfoCommand(self, rootElement, commandDevice);
        }
      }
    }
  }
}

//---------------------------------------------------------------------------
void vtkPlusServerLauncherRemoteControl::StartServerCommand(vtkPlusServerLauncherRemoteControl* self, vtkXMLDataElement* startServerCommandElement, igtlio::CommandDevicePointer commandDevice)
{
  LOG_DEBUG("Server Start command received");

  vtkSmartPointer<vtkXMLDataElement> startServerResponseElement = vtkSmartPointer<vtkXMLDataElement>::New();
  startServerResponseElement->SetName("Command");

  vtkSmartPointer<vtkXMLDataElement> plusConfigurationResponseElement = vtkSmartPointer<vtkXMLDataElement>::New();
  plusConfigurationResponseElement->SetName("PlusConfiguration");
  startServerResponseElement->AddNestedElement(plusConfigurationResponseElement);

  std::string configFileContents = "";
  vtkSmartPointer<vtkXMLDataElement> configFileElement = startServerCommandElement->FindNestedElementWithName("PlusConfiguration");
  if (!configFileElement)
  {
    LOG_ERROR("No \"PlusConfiguration\" elements found!");
    plusConfigurationResponseElement->SetAttribute("Success", "false");
    vtkPlusServerLauncherRemoteControl::RespondToCommand(self, commandDevice, startServerResponseElement);
    return;
  }
  else
  {
    std::stringstream configFileContentsStream;
    vtkXMLUtilities::FlattenElement(configFileElement, configFileContentsStream);
    configFileContents = configFileContentsStream.str();
  }

  // Log level found in <LogLevel Value=X /> format
  int logLevel = 0;
  vtkXMLDataElement* logLevelElement = startServerCommandElement->FindNestedElementWithName("LogLevel");
  if (logLevelElement)
  {
    vtkSmartPointer<vtkXMLDataElement> logLevelResponseElement = vtkSmartPointer<vtkXMLDataElement>::New();
    logLevelResponseElement->SetName("LogLevel");

    logLevelElement->GetScalarAttribute("Value", logLevel);

    if (logLevel != 0)
    {
      QMetaObject::invokeMethod(self->MainWindow,
        "SetLogLevel",
        Qt::BlockingQueuedConnection,
        Q_ARG(int, logLevel));
      logLevelResponseElement->SetAttribute("Success", "true");
    }
    else
    {
      logLevelResponseElement->SetAttribute("Success", "false");
    }
    startServerResponseElement->AddNestedElement(logLevelResponseElement);
  }

  // TODO: Should it be allowed to specify filename?
  std::string filenameAndPath = "";
  std::string path = vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationDirectory();
  std::string filename = "";
  PlusCommon::CreateTemporaryFilename(filename, vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationDirectory());
  filenameAndPath = path + "/" + filename;

  if (STRCASECMP(configFileContents.c_str(), "") != 0)
  {
    vtkPlusServerLauncherRemoteControl::RespondToCommand(self, commandDevice, startServerResponseElement);

    // Attempt to connect to the server, the connection process will block this thread
    QMetaObject::invokeMethod(self->MainWindow,
      "ConnectToDevicesByConfigString",
      Qt::BlockingQueuedConnection,
      Q_ARG(std::string, configFileContents),
      Q_ARG(std::string, "")
      );
  }
  else
  {
    plusConfigurationResponseElement->SetAttribute("Success", "false");
    vtkPlusServerLauncherRemoteControl::RespondToCommand(self, commandDevice, startServerResponseElement);
  }

}

//---------------------------------------------------------------------------
void vtkPlusServerLauncherRemoteControl::StopServerCommand(vtkPlusServerLauncherRemoteControl* self, vtkXMLDataElement* stopServerCommandElement, igtlio::CommandDevicePointer commandDevice)
{
  LOG_DEBUG("Server stop command received");

  vtkSmartPointer<vtkXMLDataElement> stopServerResponseElement = vtkSmartPointer<vtkXMLDataElement>::New();
  stopServerResponseElement->SetName("Command");

  vtkSmartPointer<vtkXMLDataElement> stopServerElement = vtkSmartPointer<vtkXMLDataElement>::New();
  stopServerElement->SetName("StopServer");
  stopServerElement->SetAttribute("Success", "true");
  stopServerResponseElement->AddNestedElement(stopServerElement);
  vtkPlusServerLauncherRemoteControl::RespondToCommand(self, commandDevice, stopServerResponseElement);

  QMetaObject::invokeMethod(self->MainWindow,
    "ConnectToDevicesByConfigFile",
    Qt::BlockingQueuedConnection,
    Q_ARG(std::string, ""));
}

//---------------------------------------------------------------------------
void vtkPlusServerLauncherRemoteControl::GetServerInfoCommand(vtkPlusServerLauncherRemoteControl* self, vtkXMLDataElement* getServerInfoCommandElement, igtlio::CommandDevicePointer commandDevice)
{
  LOG_DEBUG("Get server info command received");

  vtkSmartPointer<vtkXMLDataElement> getServerInfoResponseElement = vtkSmartPointer<vtkXMLDataElement>::New();
  getServerInfoResponseElement->SetName("Command");

  std::string status = "";
  switch (self->MainWindow->GetServerStatus())
  {
  case QProcess::ProcessState::Starting:
    status = "Starting";
    break;
  case QProcess::ProcessState::Running:
    status = "Running";
  case QProcess::ProcessState::NotRunning:
  default:
    status = "Not running";
  }

  std::string error = "";
  switch (self->MainWindow->GetServerError())
  {
  case QProcess::ProcessError::Crashed:
    error = "Crashed";
    break;
  case QProcess::ProcessError::FailedToStart:
    error = "Failed to start";
    break;
  case QProcess::ProcessError::ReadError:
    error = "Read error";
    break;
  case QProcess::ProcessError::Timedout:
    error = "Timed out";
    break;
  case QProcess::ProcessError::WriteError:
      error = "Write error";
      break;
  case QProcess::ProcessError::UnknownError:
    error = "Unknown error";
    break;
  default:
    error = "None";
  }

  vtkSmartPointer<vtkXMLDataElement> serverStatusElement = vtkSmartPointer<vtkXMLDataElement>::New();
  serverStatusElement->SetAttribute("Status", status.c_str());
  serverStatusElement->SetAttribute("Error", error.c_str());
  getServerInfoResponseElement->AddNestedElement(serverStatusElement);

  // TODO: check currently running server instance for port
  vtkSmartPointer<vtkXMLDataElement> outgoingServerPortsElement = vtkSmartPointer<vtkXMLDataElement>::New();
  outgoingServerPortsElement->SetIntAttribute("Ports", 18944);
  getServerInfoResponseElement->AddNestedElement(outgoingServerPortsElement);

  self->RespondToCommand(self, commandDevice, getServerInfoResponseElement);
}

//---------------------------------------------------------------------------
void vtkPlusServerLauncherRemoteControl::RespondToCommand(vtkPlusServerLauncherRemoteControl* self, igtlio::CommandDevicePointer commandDevice, vtkXMLDataElement* response)
{
  igtlio::CommandConverter::ContentData contentData = commandDevice->GetContent();
  std::string commandName = contentData.name;

  std::stringstream responseStream;
  vtkXMLUtilities::FlattenElement(response, responseStream);
  self->RemoteControlServerSession->SendCommandResponse(commandDevice->GetDeviceName(), commandName, responseStream.str());
}

//---------------------------------------------------------------------------
void vtkPlusServerLauncherRemoteControl::SendServerStartupSignal(const char* filename)
{

  std::string status = "Off";
  if (this->MainWindow->GetServerStatus() == QProcess::Running)
  {
    status = "On";
  }
  else
  {
    int a = this->MainWindow->GetServerStatus();
  }

  LOG_TRACE("Server startup signal");

  vtkSmartPointer<vtkXMLDataElement> commandElement = vtkSmartPointer<vtkXMLDataElement>::New();
  commandElement->SetName("Command");
  vtkSmartPointer<vtkXMLDataElement> startServerElement = vtkSmartPointer<vtkXMLDataElement>::New();
  startServerElement->SetName("ServerStarted");
  startServerElement->SetAttribute("Status", status.c_str());
  startServerElement->SetAttribute("LogLevel", std::to_string(this->MainWindow->GetLogLevel()).c_str());

  vtkSmartPointer<vtkXMLDataElement> configFileElement = NULL;
  if (filename)
  {
    configFileElement = vtkSmartPointer<vtkXMLDataElement>::Take(vtkXMLUtilities::ReadElementFromFile(filename));
  }

  if (configFileElement)
  {
    std::string ports;
    for (int i = 0; i < configFileElement->GetNumberOfNestedElements(); ++i)
    {
      vtkXMLDataElement* nestedElement = configFileElement->GetNestedElement(i);
      if (strcmp(nestedElement->GetName(), "PlusOpenIGTLinkServer") == 0)
      {
        std::stringstream serverNamePrefix;
        const char* outputChannelId = nestedElement->GetAttribute("OutputChannelId");
        if (outputChannelId)
        {
          serverNamePrefix << outputChannelId;
        }
        else
        {
          serverNamePrefix << "PlusOpenIGTLinkServer";
        }
        serverNamePrefix << ":";
        const char* port = nestedElement->GetAttribute("ListeningPort");
        if (port)
        {
          ports += serverNamePrefix.str() + std::string(port) + ";";
        }
      }
    }
    startServerElement->SetAttribute("Servers", ports.c_str());
  }

  commandElement->AddNestedElement(startServerElement);

  std::stringstream signalStream;
  vtkXMLUtilities::FlattenElement(commandElement, signalStream);
  this->RemoteControlServerSession->SendCommand(PLUS_SERVER_LAUNCHER_REMOTE_DEVICE_ID, "ServerStarted", signalStream.str(), igtlio::ASYNCHRONOUS);

}

//---------------------------------------------------------------------------
void vtkPlusServerLauncherRemoteControl::SendServerShutdownSignal()
{
  std::string status = "Off";
  int a = this->MainWindow->GetServerStatus();
  if (this->MainWindow->GetServerStatus() == QProcess::Running)
  {
    status = "On";
  }

  LOG_TRACE("Server shutdown signal");
  vtkSmartPointer<vtkXMLDataElement> commandElement = vtkSmartPointer<vtkXMLDataElement>::New();
  commandElement->SetName("Command");
  vtkSmartPointer<vtkXMLDataElement> stopServerElement = vtkSmartPointer<vtkXMLDataElement>::New();
  stopServerElement->SetName("ServerStopped");
  stopServerElement->SetAttribute("Status", status.c_str());
  stopServerElement->SetAttribute("LogLevel", std::to_string(this->MainWindow->GetLogLevel()).c_str());
  commandElement->AddNestedElement(stopServerElement);

  std::stringstream signalStream;
  vtkXMLUtilities::FlattenElement(commandElement, signalStream);
  this->RemoteControlServerSession->SendCommand(PLUS_SERVER_LAUNCHER_REMOTE_DEVICE_ID, "ServerStopped", signalStream.str(), igtlio::ASYNCHRONOUS);
}

//----------------------------------------------------------------------------
void vtkPlusServerLauncherRemoteControl::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//---------------------------------------------------------------------------
void vtkPlusServerLauncherRemoteControl::OnLogEvent(vtkObject* caller, unsigned long eventId, void* clientData, void* callData)
{
  vtkPlusServerLauncherRemoteControl* self = reinterpret_cast<vtkPlusServerLauncherRemoteControl*>(clientData);
  const char* rawLogMessage = static_cast<char*>(callData);
  std::string logString = std::string(rawLogMessage);

  if (logString.empty())
  {
    return;
  }

  typedef std::vector<std::string> StringList;
  StringList tokens;

  if (logString.find('|') == std::string::npos)
  {
    return;
  }

  PlusCommon::SplitStringIntoTokens(logString, '|', tokens, false);
  if (tokens.size() == 0)
  {
    return;
  }

  std::string logLevel = tokens[0];
  std::string messageOrigin;
  if (tokens.size() > 2 && logString.find("SERVER>") != std::string::npos)
  {
    messageOrigin = "SERVER";
  }
  else
  {
    messageOrigin = "LAUNCHER";
  }

  std::stringstream message;
  for (int i = 1; i < tokens.size(); ++i)
  {
    message << "|" << tokens[i];
  }
  std::string logMessage = message.str();

  vtkSmartPointer<vtkXMLDataElement> commandElement = vtkSmartPointer<vtkXMLDataElement>::New();
  commandElement->SetName("Command");

  vtkSmartPointer<vtkXMLDataElement> messageElement = vtkSmartPointer<vtkXMLDataElement>::New();
  messageElement->SetName("LogMessage");
  messageElement->SetAttribute("Text", logMessage.c_str());
  messageElement->SetAttribute("LogLevel", logLevel.c_str());
  messageElement->SetAttribute("Origin", messageOrigin.c_str());
  commandElement->AddNestedElement(messageElement);

  std::stringstream messageCommand;
  vtkXMLUtilities::FlattenElement(commandElement, messageCommand);
  self->RemoteControlServerSession->SendCommand(PLUS_SERVER_LAUNCHER_REMOTE_DEVICE_ID, "LogMessage", messageCommand.str(), igtlio::ASYNCHRONOUS);
}
