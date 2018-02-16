
#include <vtkPlusServerLauncherRemoteControl.h>

#include <vtkObjectFactory.h>

#include <QPlusDeviceSetSelectorWidget.h>
#include <vtkPlusLogger.h>

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

  this->RemoteControlServerLogic = igtlio::LogicPointer::New();
  this->RemoteControlServerSession = this->RemoteControlServerLogic->StartServer(this->ServerPort);
  //this->RemoteControlServerConnector = this->RemoteControlServerSession->GetConnector();

  this->RemoteControlServerLogic->AddObserver(igtlio::Logic::CommandReceivedEvent, this->RemoteControlServerCallbackCommand);
  this->RemoteControlServerLogic->AddObserver(igtlio::Logic::CommandResponseReceivedEvent, this->RemoteControlServerCallbackCommand);
  this->RemoteControlServerLogic->AddObserver(igtlio::Connector::ConnectedEvent, this->RemoteControlServerCallbackCommand);
  this->RemoteControlServerLogic->AddObserver(igtlio::Connector::DisconnectedEvent, this->RemoteControlServerCallbackCommand);

  // Create thread to receive commands
  this->Threader = vtkSmartPointer<vtkMultiThreader>::New();
  this->RemoteControlActive = std::make_pair(false, false);
  this->RemoteControlActive.first = true;
  this->Threader->SpawnThread((vtkThreadFunctionType)&PlusRemoteThread, this);

  return PLUS_SUCCESS;
}

//---------------------------------------------------------------------------
PlusStatus vtkPlusServerLauncherRemoteControl::StopRemoteControlServer()
{
  return PLUS_SUCCESS;
}

//---------------------------------------------------------------------------
void* vtkPlusServerLauncherRemoteControl::PlusRemoteThread(vtkMultiThreader::ThreadInfo* data)
{
  vtkPlusServerLauncherRemoteControl* self = (vtkPlusServerLauncherRemoteControl*)(data->UserData);
  LOG_INFO("Remote control started");

  std::ifstream logFileStream(vtkPlusLogger::Instance()->GetLogFileName());
  bool isOpen = logFileStream.is_open();
  std::string line;

  self->RemoteControlActive.second = true;
  while (self->RemoteControlActive.first)
  {
    self->RemoteControlServerLogic->PeriodicProcess();
    vtkPlusAccurateTimer::DelayWithEventProcessing(0.2);
    bool logUpdated = false;
    std::stringstream logString;
    while (std::getline(logFileStream, line))
    {
      if (STRCASECMP(line.c_str(), "") != 0)
      {
        logString << line << "###NEWLINE###";
        logUpdated = true;
      }
    }
    if(logFileStream.eof())
      logFileStream.clear();

    if (logUpdated)
    {
      vtkSmartPointer<vtkXMLDataElement> commandElement = vtkSmartPointer<vtkXMLDataElement>::New();
      commandElement->SetName("Command");
      vtkSmartPointer<vtkXMLDataElement> messageElement = vtkSmartPointer<vtkXMLDataElement>::New();
      messageElement->SetName("Message");
      messageElement->SetAttribute("Text", logString.str().c_str());
      commandElement->AddNestedElement(messageElement);

      std::stringstream messageCommand;
      vtkXMLUtilities::FlattenElement(commandElement, messageCommand);
      
      std::stringstream ss;
      commandElement->Print(ss);
      self->RemoteControlServerSession->SendCommand("LOG", "LOG", messageCommand.str(), igtlio::ASYNCHRONOUS);
    }
  }
  self->RemoteControlActive.second = false;
  LOG_INFO("Remote control stopped");

  return NULL;
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
        vtkPlusServerLauncherRemoteControl::ParseCommand(self, commandDevice);
      }
      logic->RemoveDevice(i);
    }
  }
}

//---------------------------------------------------------------------------
void vtkPlusServerLauncherRemoteControl::ParseCommand(vtkPlusServerLauncherRemoteControl* self, igtlio::CommandDevicePointer commandDevice)
{
  igtlio::CommandConverter::ContentData contentData = commandDevice->GetContent();
  int id = contentData.id;
  std::string commandName = contentData.name;
  std::string content = contentData.content;

  vtkSmartPointer<vtkXMLDataElement> commandResponseElementRoot = vtkSmartPointer<vtkXMLDataElement>::New();
  commandResponseElementRoot->SetName("Command");

  vtkSmartPointer<vtkXMLDataElement> rootElement = vtkSmartPointer<vtkXMLDataElement>::Take(vtkXMLUtilities::ReadElementFromString(content.c_str()));
  if (!rootElement)
  {
    LOG_ERROR("ParseCommand: Error parsing xml");
    return;
  }

  //TODO: Command names should be stored in a variable somewhere else
  vtkSmartPointer<vtkXMLDataElement> startServerElement = rootElement->FindNestedElementWithName("StartServer");
  vtkSmartPointer<vtkXMLDataElement> stopServerElement = rootElement->FindNestedElementWithName("StopServer");
  if (startServerElement)
  {
    self->StartServerCommand(self, startServerElement, commandResponseElementRoot);
  }
  else if (stopServerElement)
  {
    self->StopServerCommand(self, stopServerElement, commandResponseElementRoot);
  }

  self->GetCommand(self, rootElement, commandResponseElementRoot);

  // No recognized command. Nothing to reply to.
  if (commandResponseElementRoot->GetNumberOfNestedElements() == 0)
  {
    return;
  }

  vtkPlusServerLauncherRemoteControl::RespondToCommand(self, commandDevice, commandResponseElementRoot);
}

//---------------------------------------------------------------------------
void vtkPlusServerLauncherRemoteControl::StartServerCommand(vtkPlusServerLauncherRemoteControl* self, vtkXMLDataElement* startServerElement, vtkXMLDataElement* commandResponseElementRoot)
{
  LOG_INFO("Server Start command received");

  int logLevel = 0;
  if (startServerElement->GetScalarAttribute("LogLevel", logLevel))
  {
    QMetaObject::invokeMethod(self->MainWindow,
      "SetLogLevel",
      Qt::BlockingQueuedConnection,
      Q_ARG(int, logLevel));
  }

  // Get the filename from the command. The file will be saved to the config file directory with the specified name.
  // If the file already exists, it will be overwritten.
  // If no name is specified, it will be created as a .tmp file in the output directory.
  std::string fileNameAndPath = "";
  const char* fileName = startServerElement->GetAttribute("FileName");
  if (fileName)
  {
    std::string path = vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationDirectory();
    fileNameAndPath = path + "/" + fileName;
    LOG_TRACE("Writing config file to: " << fileName);
  }

  // Contents of the config file. Identified by root element "PlusConfiguration"
  std::string configFileContents = "None";
  vtkSmartPointer<vtkXMLDataElement> configFileElement = startServerElement->FindNestedElementWithName("PlusConfiguration");
  if (configFileElement)
  {
    std::stringstream configFileContentsStream;
    vtkXMLUtilities::FlattenElement(configFileElement, configFileContentsStream);
    configFileContents = configFileContentsStream.str();
  }

  if (STRCASECMP(configFileContents.c_str(), "None") == 0)
  {
    LOG_TRACE("No config file contents found!");
    if (STRCASECMP(fileNameAndPath.c_str(), "") != 0)
    {
      // Attempt to connect to the server, the connection process will block this thread
      LOG_TRACE("Attempting to start server using: " << fileNameAndPath);
      PlusStatus serverStartSuccess = PLUS_FAIL;
      QMetaObject::invokeMethod(self->MainWindow,
        "ConnectToDevicesByConfigFile",
        Qt::BlockingQueuedConnection,
        Q_ARG(std::string, fileNameAndPath));
    }
    else
    {
      // TODO: what to do if user doesn't specify config file?
      // Activate with currently selected config file?
      LOG_TRACE("No config file name found!");
      LOG_TRACE("Attempting to connect using the currently selected config file.");
      QMetaObject::invokeMethod(self->DeviceSetSelectorWidget,
        "ConnectToDevicesByConfigFile",
        Qt::BlockingQueuedConnection,
        Q_ARG(std::string, vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationFileName()));
    }

    std::string serverStarted = "false";
    if (self->MainWindow->GetServerStatus() == QProcess::Running)
    {
      serverStarted = "true";
    }

    vtkSmartPointer<vtkXMLDataElement> startServerResponse = vtkSmartPointer<vtkXMLDataElement>::New();
    startServerResponse->SetName("StartServer");
    startServerResponse->SetAttribute("Success", serverStarted.c_str());
    commandResponseElementRoot->AddNestedElement(startServerResponse);
  }
  else
  {
    // Attempt to connect to the server, the connection process will block this thread
    QMetaObject::invokeMethod(self->MainWindow,
      "ConnectToDevicesByConfigString",
      Qt::BlockingQueuedConnection,
      Q_ARG(std::string, configFileContents),
      Q_ARG(std::string, fileNameAndPath)
      );

    vtkSmartPointer<vtkXMLDataElement> startServerResponse = vtkSmartPointer<vtkXMLDataElement>::New();
    startServerResponse->SetName("StartServer");
    commandResponseElementRoot->AddNestedElement(startServerResponse);
  }
}

//---------------------------------------------------------------------------
void vtkPlusServerLauncherRemoteControl::StopServerCommand(vtkPlusServerLauncherRemoteControl* self, vtkXMLDataElement* startServerElement, vtkXMLDataElement* commandResponseElementRoot)
{
  //TODO: Any info to read for stop command?
  LOG_INFO("Server stop command received");

  QMetaObject::invokeMethod(self->MainWindow,
    "ConnectToDevicesByConfigFile",
    Qt::BlockingQueuedConnection,
    Q_ARG(std::string, ""));

  vtkSmartPointer<vtkXMLDataElement> stopServerResponse = vtkSmartPointer<vtkXMLDataElement>::New();
  stopServerResponse->SetName("StopServer");
  commandResponseElementRoot->AddNestedElement(stopServerResponse);
}

//---------------------------------------------------------------------------
void vtkPlusServerLauncherRemoteControl::GetCommand(vtkPlusServerLauncherRemoteControl* self, vtkXMLDataElement* rootElement, vtkXMLDataElement* commandResponseElementRoot)
{
  // Look for all of the "Get" statements
  for (int i = 0; i < rootElement->GetNumberOfNestedElements(); ++i)
  {
    vtkSmartPointer<vtkXMLDataElement> nestedElement = rootElement->GetNestedElement(i);
    if (STRCASECMP(nestedElement->GetName(), "Get") == 0)
    {
      const char* parameterName = nestedElement->GetAttribute("Name");
      if (parameterName)
      {
        LOG_TRACE("Command received: Get(" << parameterName << ")");

        vtkSmartPointer<vtkXMLDataElement> getResponseElement = vtkSmartPointer<vtkXMLDataElement>::New();
        if (STRCASECMP(parameterName, "Status") == 0)
        {
          const char* status = "Off";
          if (self->MainWindow->GetServerStatus() == QProcess::Running)
          {
            status = "On";
          }
          getResponseElement->SetName(nestedElement->GetName());
          getResponseElement->SetAttribute("Status", status);
        }
        else
        {
          // Not a recognized parameter, move to the next "Get" element
          continue;
        }
        commandResponseElementRoot->AddNestedElement(getResponseElement);
      }
    }
  }
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
void vtkPlusServerLauncherRemoteControl::SendServerStartupSignal()
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
  commandElement->AddNestedElement(startServerElement);

  std::stringstream signalStream;
  vtkXMLUtilities::FlattenElement(commandElement, signalStream);
  this->RemoteControlServerSession->SendCommand(this->GetCommandName(), "ServerStarted", signalStream.str(), igtlio::ASYNCHRONOUS);

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
  this->RemoteControlServerSession->SendCommand(this->GetCommandName(), "ServerStopped", signalStream.str(), igtlio::ASYNCHRONOUS);
}

//----------------------------------------------------------------------------
std::string vtkPlusServerLauncherRemoteControl::GetCommandName()
{
  std::stringstream nameStream;
  //nameStream << "CMD_";
  //nameStream << this->CommandId;
  nameStream << "RemoteControl";
  return nameStream.str();
}

//----------------------------------------------------------------------------
void vtkPlusServerLauncherRemoteControl::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}
