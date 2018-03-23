/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

// Local includes
#include "PlusServerLauncherMainWindow.h"

// PlusLib includes
#include <PlusCommon.h>
#include <QPlusDeviceSetSelectorWidget.h>
#include <QPlusStatusIcon.h>
#include <vtkPlusDataCollector.h>
#include <vtkPlusDeviceFactory.h>
#include <vtkPlusOpenIGTLinkServer.h>
#include <vtkPlusTransformRepository.h>

// Qt includes
#include <QComboBox>
#include <QFileInfo>
#include <QHostAddress>
#include <QHostInfo>
#include <QIcon>
#include <QKeyEvent>
#include <QNetworkInterface>
#include <QProcess>
#include <QRegExp>
#include <QStringList>
#include <QTimer>

// VTK include
#include <vtkMultiThreader.h>

// STL includes
#include <algorithm>

// OpenIGTLinkIO includes
#include <igtlioDevice.h>

namespace
{
  void ReplaceStringInPlace(std::string& subject, const std::string& search, const std::string& replace)
  {
    size_t pos = 0;
    while ((pos = subject.find(search, pos)) != std::string::npos)
    {
      subject.replace(pos, search.length(), replace);
      pos += replace.length();
    }
  }
}

void TEST_START_CLIENT();
//-----------------------------------------------------------------------------
PlusServerLauncherMainWindow::PlusServerLauncherMainWindow(QWidget* parent /*=0*/, Qt::WindowFlags flags/*=0*/, bool autoConnect /*=false*/, int remoteControlServerPort/*=RemoteControlServerPortUseDefault*/)
  : QMainWindow(parent, flags | Qt::WindowMinimizeButtonHint | Qt::WindowCloseButtonHint)
  , m_DeviceSetSelectorWidget(NULL)
  , m_CurrentServerInstance(NULL)
  , m_RemoteControlServerPort(remoteControlServerPort)
{
  m_RemoteControlServerCallbackCommand = vtkSmartPointer<vtkCallbackCommand>::New();
  m_RemoteControlServerCallbackCommand->SetCallback(PlusServerLauncherMainWindow::OnRemoteControlServerEventReceived);
  m_RemoteControlServerCallbackCommand->SetClientData(this);

  // Set up UI
  ui.setupUi(this);

  // Create device set selector widget
  m_DeviceSetSelectorWidget = new QPlusDeviceSetSelectorWidget(NULL);
  m_DeviceSetSelectorWidget->setMaximumWidth(1200);
  m_DeviceSetSelectorWidget->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
  m_DeviceSetSelectorWidget->SetConnectButtonText(QString("Launch server"));
  connect(m_DeviceSetSelectorWidget, SIGNAL(ConnectToDevicesByConfigFileInvoked(std::string)), this, SLOT(ConnectToDevicesByConfigFile(std::string)));

  // Create status icon
  QPlusStatusIcon* statusIcon = new QPlusStatusIcon(NULL);
  // Show only the last few thousand messages
  // (it should be enough, as all the messages are available in log files anyway)
  statusIcon->SetMaxMessageCount(3000);

  // Put the status icon in a frame with the log level selector
  ui.statusBarLayout->insertWidget(0, statusIcon);
  ui.comboBox_LogLevel->addItem("Error", QVariant(vtkPlusLogger::LOG_LEVEL_ERROR));
  ui.comboBox_LogLevel->addItem("Warning", QVariant(vtkPlusLogger::LOG_LEVEL_WARNING));
  ui.comboBox_LogLevel->addItem("Info", QVariant(vtkPlusLogger::LOG_LEVEL_INFO));
  ui.comboBox_LogLevel->addItem("Debug", QVariant(vtkPlusLogger::LOG_LEVEL_DEBUG));
  ui.comboBox_LogLevel->addItem("Trace", QVariant(vtkPlusLogger::LOG_LEVEL_TRACE));
  if (autoConnect)
  {
    ui.comboBox_LogLevel->setCurrentIndex(ui.comboBox_LogLevel->findData(QVariant(vtkPlusLogger::Instance()->GetLogLevel())));
  }
  else
  {
    ui.comboBox_LogLevel->setCurrentIndex(ui.comboBox_LogLevel->findData(QVariant(vtkPlusLogger::LOG_LEVEL_INFO)));
    vtkPlusLogger::Instance()->SetLogLevel(vtkPlusLogger::LOG_LEVEL_INFO);
  }
  connect(ui.comboBox_LogLevel, SIGNAL(currentIndexChanged(int)), this, SLOT(LogLevelChanged()));

  // Insert widgets into placeholders
  ui.centralLayout->setMargin(4);
  ui.centralLayout->insertWidget(0, m_DeviceSetSelectorWidget);

  // Log basic info (Plus version, supported devices)
  std::string strPlusLibVersion = std::string(" Software version: ") + PlusCommon::GetPlusLibVersionString();
  LOG_INFO(strPlusLibVersion);
  LOG_INFO("Logging at level " << vtkPlusLogger::Instance()->GetLogLevel() << " to file: " << vtkPlusLogger::Instance()->GetLogFileName());
  vtkSmartPointer<vtkPlusDeviceFactory> deviceFactory = vtkSmartPointer<vtkPlusDeviceFactory>::New();
  std::ostringstream supportedDevices;
  deviceFactory->PrintAvailableDevices(supportedDevices, vtkIndent());
  LOG_INFO(supportedDevices.str());

  if (autoConnect)
  {
    std::string configFileName = vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationFileName();
    if (configFileName.empty())
    {
      LOG_ERROR("Auto-connect failed: device set configuration file is not specified");
    }
    else
    {
      ConnectToDevicesByConfigFile(configFileName);
      if (m_DeviceSetSelectorWidget->GetConnectionSuccessful())
      {
        showMinimized();
      }
    }
  }

  // Log server host name, domain, and IP addresses
  LOG_INFO("Server host name: " << QHostInfo::localHostName().toLatin1().constData());
  if (!QHostInfo::localDomainName().isEmpty())
  {
    LOG_INFO("Server host domain: " << QHostInfo::localDomainName().toLatin1().constData());
  }

  QString ipAddresses;
  QList<QHostAddress> list = QNetworkInterface::allAddresses();
  for (int hostIndex = 0; hostIndex < list.count(); hostIndex++)
  {
    if (list[hostIndex].protocol() == QAbstractSocket::IPv4Protocol)
    {
      if (!ipAddresses.isEmpty())
      {
        ipAddresses.append(",  ");
      }
      ipAddresses.append(list[hostIndex].toString());
    }
  }

  LOG_INFO("Server IP addresses: " << ipAddresses.toLatin1().constData());

  if (m_RemoteControlServerPort != PlusServerLauncherMainWindow::RemoteControlServerPortDisable)
  {
    if (m_RemoteControlServerPort == PlusServerLauncherMainWindow::RemoteControlServerPortUseDefault)
    {
      m_RemoteControlServerPort = DEFAULT_REMOTE_CONTROL_SERVER_PORT;
    }
    if (!this->StartRemoteControlServer())
    {
      LOG_ERROR("Remote control server could not be started!")
    }
  }


  TEST_START_CLIENT();

}

//-----------------------------------------------------------------------------
PlusServerLauncherMainWindow::~PlusServerLauncherMainWindow()
{
  StopServer(); // deletes m_CurrentServerInstance

  if (m_RemoteControlServerLogic)
  {
    m_RemoteControlServerLogic->RemoveObserver(m_RemoteControlServerCallbackCommand);
  }

  if (m_DeviceSetSelectorWidget != NULL)
  {
    delete m_DeviceSetSelectorWidget;
    m_DeviceSetSelectorWidget = NULL;
  }

  // Wait for remote control thread to terminate
  m_RemoteControlActive.first = false;
  while (m_RemoteControlActive.second)
  {
    vtkPlusAccurateTimer::DelayWithEventProcessing(0.2);
  }
}

//-----------------------------------------------------------------------------
bool PlusServerLauncherMainWindow::StartServer(const QString& configFilePath)
{
  if (m_CurrentServerInstance != NULL)
  {
    StopServer();
  }

  m_CurrentServerInstance = new QProcess();
  std::string plusServerExecutable = vtkPlusConfig::GetInstance()->GetPlusExecutablePath("PlusServer");
  std::string plusServerLocation = vtksys::SystemTools::GetFilenamePath(plusServerExecutable);
  m_CurrentServerInstance->setWorkingDirectory(QString(plusServerLocation.c_str()));

  connect(m_CurrentServerInstance, SIGNAL(readyReadStandardOutput()), this, SLOT(StdOutMsgReceived()));
  connect(m_CurrentServerInstance, SIGNAL(readyReadStandardError()), this, SLOT(StdErrMsgReceived()));
  connect(m_CurrentServerInstance, SIGNAL(error(QProcess::ProcessError)), this, SLOT(ErrorReceived(QProcess::ProcessError)));
  connect(m_CurrentServerInstance, SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(ServerExecutableFinished(int, QProcess::ExitStatus)));

  // PlusServerLauncher wants at least LOG_LEVEL_INFO to parse status information from the PlusServer executable
  // Un-requested log entries that are captured from the PlusServer executable are parsed and dropped from output
  int logLevelToPlusServer = std::max<int>(ui.comboBox_LogLevel->currentData().toInt(), vtkPlusLogger::LOG_LEVEL_INFO);
  QString cmdLine = QString("\"%1\" --config-file=\"%2\" --verbose=%3").arg(plusServerExecutable.c_str()).arg(configFilePath).arg(logLevelToPlusServer);
  LOG_INFO("Server process command line: " << cmdLine.toLatin1().constData());
  m_CurrentServerInstance->start(cmdLine);
  m_CurrentServerInstance->waitForFinished(500);

  // During waitForFinished an error signal may be emitted, which may delete m_CurrentServerInstance,
  // therefore we need to check if m_CurrentServerInstance is still not NULL
  if (m_CurrentServerInstance && m_CurrentServerInstance->state() == QProcess::Running)
  {
    LOG_INFO("Server process started successfully");
    ui.comboBox_LogLevel->setEnabled(false);
    return true;
  }
  else
  {
    LOG_ERROR("Failed to start server process");
    return false;
  }
}

//-----------------------------------------------------------------------------
bool PlusServerLauncherMainWindow::StopServer()
{
  if (m_CurrentServerInstance == NULL)
  {
    // already stopped
    return true;
  }

  disconnect(m_CurrentServerInstance, SIGNAL(readyReadStandardOutput()), this, SLOT(StdOutMsgReceived()));
  disconnect(m_CurrentServerInstance, SIGNAL(readyReadStandardError()), this, SLOT(StdErrMsgReceived()));
  disconnect(m_CurrentServerInstance, SIGNAL(error(QProcess::ProcessError)), this, SLOT(ErrorReceived(QProcess::ProcessError)));
  disconnect(m_CurrentServerInstance, SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(ServerExecutableFinished(int, QProcess::ExitStatus)));

  bool forcedShutdown = false;
  if (m_CurrentServerInstance->state() == QProcess::Running)
  {
    m_CurrentServerInstance->terminate();
    if (m_CurrentServerInstance->state() == QProcess::Running)
    {
      LOG_INFO("Server process stop request sent successfully");
    }
    const int totalTimeoutSec = 15;
    const double retryDelayTimeoutSec = 0.3;
    double timePassedSec = 0;
    while (!m_CurrentServerInstance->waitForFinished(retryDelayTimeoutSec * 1000))
    {
      m_CurrentServerInstance->terminate(); // in release mode on Windows the first terminate request may go unnoticed
      timePassedSec += retryDelayTimeoutSec;
      if (timePassedSec > totalTimeoutSec)
      {
        // graceful termination was not successful, force the process to quit
        LOG_WARNING("Server process did not stop on request for " << timePassedSec << " seconds, force it to quit now");
        m_CurrentServerInstance->kill();
        forcedShutdown = true;
        break;
      }
    }
    LOG_INFO("Server process stopped successfully");
    ui.comboBox_LogLevel->setEnabled(true);
  }
  delete m_CurrentServerInstance;
  m_CurrentServerInstance = NULL;
  m_Suffix.clear();
  return (!forcedShutdown);
}

//----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::ParseContent(const std::string& message)
{
  // Input is the format: message
  // Plus OpenIGTLink server listening on IPs: 169.254.100.247, 169.254.181.13, 129.100.44.163, 192.168.199.1, 192.168.233.1, 127.0.0.1 -- port 18944
  if (message.find("Plus OpenIGTLink server listening on IPs:") != std::string::npos)
  {
    m_Suffix.append(message);
    m_Suffix.append("\n");
    m_DeviceSetSelectorWidget->SetDescriptionSuffix(QString(m_Suffix.c_str()));
  }
  else if (message.find("Server status: Server(s) are running.") != std::string::npos)
  {
    m_DeviceSetSelectorWidget->SetConnectionSuccessful(true);
    m_DeviceSetSelectorWidget->SetConnectButtonText(QString("Stop server"));
  }
  else if (message.find("Server status: ") != std::string::npos)
  {
    // pull off server status and display it
    this->m_DeviceSetSelectorWidget->SetDescriptionSuffix(QString(message.c_str()));
  }
}

//-----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::ConnectToDevicesByConfigFile(std::string aConfigFile)
{
  // Either a connect or disconnect, we always start from a clean slate: delete any previously active servers
  if (m_CurrentServerInstance != NULL)
  {
    StopServer();
  }

  // Disconnect
  // Empty parameter string means disconnect from device
  if (aConfigFile.empty())
  {
    LOG_INFO("Disconnect request successful");
    m_DeviceSetSelectorWidget->ClearDescriptionSuffix();
    m_DeviceSetSelectorWidget->SetConnectionSuccessful(false);
    m_DeviceSetSelectorWidget->SetConnectButtonText(QString("Launch server"));
    return;
  }

  LOG_INFO("Connect using configuration file: " << aConfigFile);

  // Connect
  if (StartServer(QString(aConfigFile.c_str())))
  {
    m_DeviceSetSelectorWidget->SetConnectButtonText(QString("Launching..."));
  }
  else
  {
    m_DeviceSetSelectorWidget->ClearDescriptionSuffix();
    m_DeviceSetSelectorWidget->SetConnectionSuccessful(false);
    m_DeviceSetSelectorWidget->SetConnectButtonText(QString("Launch server"));
  }
}

//-----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::keyPressEvent(QKeyEvent* e)
{
  // If ESC key is pressed don't quit the application, just minimize
  if (e->key() == Qt::Key_Escape)
  {
    showMinimized();
  }
  else
  {
    QMainWindow::keyPressEvent(e);
  }
}

//-----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::SendServerOutputToLogger(const QByteArray& strData)
{
  typedef std::vector<std::string> StringList;

  QString string(strData);
  std::string logString(string.toLatin1().constData());

  if (logString.empty())
  {
    return;
  }

  // De-windows-ifiy
  ReplaceStringInPlace(logString, "\r\n", "\n");
  StringList tokens;

  if (logString.find('|') != std::string::npos)
  {
    PlusCommon::SplitStringIntoTokens(logString, '|', tokens, false);
    // Remove empty tokens
    for (StringList::iterator it = tokens.begin(); it != tokens.end(); ++it)
    {
      if (PlusCommon::Trim(*it).empty())
      {
        tokens.erase(it);
        it = tokens.begin();
      }
    }
    unsigned int index = 0;
    if (tokens.size() == 0)
    {
      LOG_ERROR("Incorrectly formatted message received from server. Cannot parse.");
      return;
    }

    if (vtkPlusLogger::GetLogLevelType(tokens[0]) != vtkPlusLogger::LOG_LEVEL_UNDEFINED)
    {
      vtkPlusLogger::LogLevelType logLevel = vtkPlusLogger::GetLogLevelType(tokens[index++]);
      std::string timeStamp("time???");
      if (tokens.size() > 1)
      {
        timeStamp = tokens[1];
      }
      std::string message("message???");
      if (tokens.size() > 2)
      {
        message = tokens[2];
      }
      std::string location("location???");
      if (tokens.size() > 3)
      {
        location = tokens[3];
      }

      if (location.find('(') == std::string::npos)
      {
        // Malformed server message, print as is
        vtkPlusLogger::Instance()->LogMessage(logLevel, message.c_str());
      }
      else
      {
        std::string file = location.substr(4, location.find_last_of('(') - 4);
        int lineNumber(0);
        std::stringstream lineNumberStr(location.substr(location.find_last_of('(') + 1, location.find_last_of(')') - location.find_last_of('(') - 1));
        lineNumberStr >> lineNumber;

        // Only parse for content if the line was successfully parsed for logging
        this->ParseContent(message);

        vtkPlusLogger::Instance()->LogMessage(logLevel, message.c_str(), file.c_str(), lineNumber, "SERVER");
      }
    }
  }
  else
  {
    PlusCommon::SplitStringIntoTokens(logString, '\n', tokens, false);
    for (StringList::iterator it = tokens.begin(); it != tokens.end(); ++it)
    {
      vtkPlusLogger::Instance()->LogMessage(vtkPlusLogger::LOG_LEVEL_INFO, *it, "SERVER");
      this->ParseContent(*it);
    }
    return;
  }
}

//-----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::StdOutMsgReceived()
{
  QByteArray strData = m_CurrentServerInstance->readAllStandardOutput();
  SendServerOutputToLogger(strData);
}

//-----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::StdErrMsgReceived()
{
  QByteArray strData = m_CurrentServerInstance->readAllStandardError();
  SendServerOutputToLogger(strData);
}

//-----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::ErrorReceived(QProcess::ProcessError errorCode)
{
  const char* errorString = "unknown";
  switch ((QProcess::ProcessError)errorCode)
  {
    case QProcess::FailedToStart:
      errorString = "FailedToStart";
      break;
    case QProcess::Crashed:
      errorString = "Crashed";
      break;
    case QProcess::Timedout:
      errorString = "Timedout";
      break;
    case QProcess::WriteError:
      errorString = "WriteError";
      break;
    case QProcess::ReadError:
      errorString = "ReadError";
      break;
    case QProcess::UnknownError:
    default:
      errorString = "UnknownError";
  }
  LOG_ERROR("Server process error: " << errorString);
  m_DeviceSetSelectorWidget->SetConnectionSuccessful(false);
}

//-----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::ServerExecutableFinished(int returnCode, QProcess::ExitStatus status)
{
  if (returnCode == 0)
  {
    LOG_INFO("Server process terminated.");
  }
  else
  {
    LOG_ERROR("Server stopped unexpectedly. Return code: " << returnCode);
  }
  this->ConnectToDevicesByConfigFile("");
  ui.comboBox_LogLevel->setEnabled(true);
  m_DeviceSetSelectorWidget->SetConnectionSuccessful(false);
}

//----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::LogLevelChanged()
{
  vtkPlusLogger::Instance()->SetLogLevel(ui.comboBox_LogLevel->currentData().toInt());
}

//---------------------------------------------------------------------------
void PlusServerLauncherMainWindow::OnRemoteControlServerEventReceived(vtkObject* caller, unsigned long eventId, void* clientData, void* callData)
{
  PlusServerLauncherMainWindow* self = reinterpret_cast<PlusServerLauncherMainWindow*>(clientData);

  auto device = dynamic_cast<igtlio::Device*>(caller);
  auto logic = dynamic_cast<igtlio::Logic*>(caller);
  auto connector = dynamic_cast<igtlio::Connector*>(caller);

  //if (device == nullptr)
  //{
  //  return;
  //}
  LOG_WARNING(eventId);

  switch (eventId)
  {
    case igtlio::Logic::CommandReceivedEvent:
      LOG_ERROR("COMMAND RECIEVED");
      if (device)
      {
        LOG_ERROR("device");
      }
      else if (logic)
      {
        for (unsigned int i = 0; i < logic->GetNumberOfDevices(); ++i)
        {
          igtlio::DevicePointer device = logic->GetDevice(i);
          if (STRCASECMP(device->GetDeviceType().c_str(), "COMMAND") == 0)
          {
            igtlio::CommandDevicePointer commandDevice = igtlio::CommandDevice::SafeDownCast(device);
            if (device->MessageDirectionIsIn())
            {
              PlusServerLauncherMainWindow::OnCommandRecieved(self, commandDevice);
            }
          }
        }

      }
      else if (connector)
      {
        LOG_ERROR("connector");
      }
      else
      {
        LOG_ERROR("NO MATCH");
      }
      break;

    case igtlio::Logic::CommandResponseReceivedEvent:
      LOG_WARNING("RESPONSE RECIEVED");
      break;

    case igtlio::Logic::ConnectionAddedEvent:
      LOG_WARNING("ADDED");
      break;

    case igtlio::Connector::ConnectedEvent:
      LOG_WARNING("CONNECTED");
      break;

    case igtlio::Connector::DisconnectedEvent:
      LOG_WARNING("DISCONNECTED");
      break;
  }
}

void PlusServerLauncherMainWindow::OnCommandRecieved(PlusServerLauncherMainWindow* self, igtlio::CommandDevicePointer command)
{

  LOG_ERROR("INCOMING COMMAND");

  igtlio::CommandConverter::ContentData content = command->GetContent();
  std::string commandName = content.name;

  if (STRCASECMP(commandName.c_str(), "StartServer") == 0)
  {
    
    if (self->ConnectToDevicesByConfigString(content.content))
    {
      igtlio::CommandDevicePointer d = self->m_RemoteControlServerSession->SendCommandResponse(command->GetDeviceName(), commandName, "<Command>\n"
                                                                                                                                      "  <Result success=true>"
                                                                                                                                      "</Command>");
      LOG_ERROR(d->GetContent().id);
      LOG_ERROR(d->GetContent().name);
      LOG_ERROR(d->GetContent().content);

    }
    else
    {
      //self->m_RemoteControlServerSession->SendCommandResponse(command->GetDeviceName(), commandName, "Server failed to start.");
    }

  }
  else if (STRCASECMP(commandName.c_str(), "StopServer") == 0)
  {
    self->StopServer();
  }

}

void TEST_RESPONSE(vtkObject* caller, unsigned long eventID, void* clientData, void* callData)
{
  //LOG_ERROR("EventID: " << eventID);
  //PlusServerLauncherMainWindow* self = reinterpret_cast<PlusServerLauncherMainWindow*>(clientData);


  auto logic = dynamic_cast<igtlio::Logic*>(caller);

  for (unsigned int i = 0; i < logic->GetNumberOfDevices(); ++i)
  {
    igtlio::DevicePointer device = logic->GetDevice(i);
    if (STRCASECMP(device->GetDeviceType().c_str(), "COMMAND") == 0)
    {
      igtlio::CommandDevicePointer commandDevice = igtlio::CommandDevice::SafeDownCast(device);
      if (device->MessageDirectionIsIn())
      {
        LOG_WARNING(commandDevice->GetContent().id);
        LOG_WARNING(commandDevice->GetContent().name);
        LOG_WARNING(commandDevice->GetContent().content);
      }
    }
  }
}

void TEST_START_CLIENT()
{

  vtkSmartPointer<vtkCallbackCommand> c = vtkSmartPointer<vtkCallbackCommand>::New();
  c->SetCallback(TEST_RESPONSE);
  c->SetClientData(NULL);

  Sleep(1000);

  igtlio::LogicPointer logic = igtlio::LogicPointer::New();
  igtlio::ConnectorPointer connector = logic->CreateConnector();
  connector->SetTypeClient("localhost", 18904);
  connector->Start();

  logic->AddObserver(igtlio::Logic::CommandResponseReceivedEvent, c);
  
  Sleep(50);
  connector->SendCommand("CMD_1", "StartServer", "<PlusConfiguration version=\"2.0\">  <DataCollection StartupDelaySec=\"1.0\" >    <DeviceSet       Name=\"PlusServer: Media Foundation video capture device - color\"      Description=\"Broadcasting acquired video through OpenIGTLink\" />    <Device      Id=\"VideoDevice\"       Type=\"MmfVideo\"       FrameSize=\"640 480\"      VideoFormat=\"YUY2\"      CaptureDeviceId=\"0\" >      <DataSources>        <DataSource Type=\"Video\" Id=\"Video\" PortUsImageOrientation=\"MF\" ImageType=\"RGB_COLOR\"  />      </DataSources>            <OutputChannels>        <OutputChannel Id=\"VideoStream\" VideoDataSourceId=\"Video\" />      </OutputChannels>    </Device>    <Device      Id=\"CaptureDevice\"      Type=\"VirtualCapture\"      BaseFilename=\"RecordingTest.mha\"      EnableCapturingOnStart=\"FALSE\" >      <InputChannels>        <InputChannel Id=\"VideoStream\" />      </InputChannels>    </Device>  </DataCollection>  <CoordinateDefinitions>    <Transform From=\"Image\" To=\"Reference\"      Matrix=\"        0.2 0.0 0.0 0.0        0.0 0.2 0.0 0.0        0.0 0.0 0.2 0.0                0 0 0 1\" />  </CoordinateDefinitions>    <PlusOpenIGTLinkServer     MaxNumberOfIgtlMessagesToSend=\"1\"     MaxTimeSpentWithProcessingMs=\"50\"     ListeningPort=\"18944\"     SendValidTransformsOnly=\"true\"     OutputChannelId=\"VideoStream\" >     <DefaultClientInfo>       <MessageTypes>         <Message Type=\"IMAGE\" />      </MessageTypes>      <ImageNames>        <Image Name=\"Image\" EmbeddedTransformToFrame=\"Reference\" />      </ImageNames>    </DefaultClientInfo>  </PlusOpenIGTLinkServer></PlusConfiguration>");
  Sleep(50);
  
  for (int i = 0; i < 10; ++i)
  {
    logic->PeriodicProcess();
    //connector->RequestPushOutgoingMessages();
    vtkPlusAccurateTimer::DelayWithEventProcessing(0.2);
  }

  connector->Stop();
  //deviceFactory->Delete();
}


//---------------------------------------------------------------------------
PlusStatus PlusServerLauncherMainWindow::StartRemoteControlServer()
{

  igtlio::SessionPointer p = igtlio::SessionPointer::New();
  
  LOG_INFO("Start remote control server at port: " << m_RemoteControlServerPort);
  m_RemoteControlServerLogic = igtlio::LogicPointer::New();
  m_RemoteControlServerLogic->AddObserver(igtlio::Logic::CommandReceivedEvent, m_RemoteControlServerCallbackCommand);
  m_RemoteControlServerLogic->AddObserver(igtlio::Logic::CommandResponseReceivedEvent, m_RemoteControlServerCallbackCommand);
  m_RemoteControlServerSession = m_RemoteControlServerLogic->StartServer(m_RemoteControlServerPort);
  //m_RemoteControlServerConnector->SetTypeServer(m_RemoteControlServerPort);
  //m_RemoteControlServerConnector->Start();

  m_RemoteControlServerConnector = m_RemoteControlServerLogic->CreateConnector();
  m_RemoteControlServerConnector->AddObserver(igtlio::Connector::ConnectedEvent, m_RemoteControlServerCallbackCommand);
  m_RemoteControlServerConnector->AddObserver(igtlio::Connector::DisconnectedEvent, m_RemoteControlServerCallbackCommand);
  m_RemoteControlServerConnector->AddObserver(igtlio::Connector::DeviceContentModifiedEvent, m_RemoteControlServerCallbackCommand);
  m_RemoteControlServerConnector->AddObserver(igtlio::Connector::NewDeviceEvent, m_RemoteControlServerCallbackCommand);
  m_RemoteControlServerConnector->AddObserver(igtlio::Connector::RemovedDeviceEvent, m_RemoteControlServerCallbackCommand);

  // Create thread to receive commands
  m_Threader = vtkSmartPointer<vtkMultiThreader>::New();
  m_RemoteControlActive = std::make_pair(false, false);
  m_RemoteControlActive.first = true;
  m_Threader->SpawnThread((vtkThreadFunctionType)&PlusRemoteThread, this);

  return PLUS_SUCCESS;
}

//---------------------------------------------------------------------------
void* PlusServerLauncherMainWindow::PlusRemoteThread(vtkMultiThreader::ThreadInfo* data)
{
  PlusServerLauncherMainWindow* self = (PlusServerLauncherMainWindow*)(data->UserData);
  LOG_INFO("Remote control started");

  self->m_RemoteControlActive.second = true;
  while (self->m_RemoteControlActive.first)
  {
    self->m_RemoteControlServerLogic->PeriodicProcess();  
    vtkPlusAccurateTimer::DelayWithEventProcessing(0.2);
  }
  self->m_RemoteControlActive.second = false;

  LOG_INFO("Remote control stopped");

  return NULL;
}

//---------------------------------------------------------------------------
PlusStatus PlusServerLauncherMainWindow::ConnectToDevicesByConfigString(const std::string& configFileString)
{
  std::string filename;
  PlusCommon::CreateTemporaryFilename(filename, vtkPlusConfig::GetInstance()->GetOutputDirectory());

  ofstream file;
  file.open(filename);
  file << configFileString;
  file.close();

  QString qFilename = QString(filename.c_str());
  this->ConnectToDevicesByConfigFile(filename);

  return PLUS_SUCCESS;
}