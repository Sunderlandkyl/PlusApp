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

//-----------------------------------------------------------------------------
PlusServerLauncherMainWindow::PlusServerLauncherMainWindow(QWidget* parent /*=0*/, Qt::WindowFlags flags/*=0*/, bool autoConnect /*=false*/, int remoteControlServerPort/*=RemoteControlServerPortUseDefault*/)
  : QMainWindow(parent, flags | Qt::WindowMinimizeButtonHint | Qt::WindowCloseButtonHint)
  , m_DeviceSetSelectorWidget(NULL)
  , m_CurrentServerInstance(NULL)
  , m_RemoteControlServerPort(remoteControlServerPort)
{

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
      this->ConnectToDevicesByConfigFile(configFileName);
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

  m_CommandId = 0;
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

    // If the remote controller is still running, send a command to all connected controllers to let them know that the server has been stopped manually.
    if (m_RemoteControlActive.second)
    {
      //LOG_ERROR("SERVER SHUTDOWN WITH CONNECTION");
      /*m_Connections*/
      for (std::vector<igtlio::ConnectorPointer>::iterator connection = m_Connections.begin(); connection != m_Connections.end(); ++connection)
      {
        std::stringstream deviceNameStream;
        deviceNameStream << "CMD_" << m_CommandId;
        (*connection)->SendCommand(deviceNameStream.str(), "ServerStopped", "<Command><Test /></Command>");
      }
      ++m_CommandId;
    }
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
    m_DeviceSetSelectorWidget->SetDescriptionSuffix(QString(message.c_str()));
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

//---------------------------------------------------------------------------
PlusStatus PlusServerLauncherMainWindow::ConnectToDevicesByConfigString(std::string configFileString, std::string filename)
{
  //std::string filename;

  if (STRCASECMP("", filename.c_str()) == 0)
  {
    PlusCommon::CreateTemporaryFilename(filename, vtkPlusConfig::GetInstance()->GetOutputDirectory());
    LOG_INFO("Creating temporary file: " << filename);
  }

  //m_DeviceSetSelectorWidget->Co

  // Write contents of server config elements to file
  ofstream file;
  file.open(filename);
  file << configFileString;
  file.close();

  this->ConnectToDevicesByConfigFile(filename);

  if (m_CurrentServerInstance && m_CurrentServerInstance->state() == QProcess::Running)
  {
    return PLUS_SUCCESS;
  }

  // TODO: update UI to show the name of the config file that was passed via string
  //vtkPlusConfig::GetInstance()->SetDeviceSetConfigurationFileName(filename);
  return PLUS_FAIL;

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

void PlusServerLauncherMainWindow::SetLogLevel(int logLevel)
{
  this->ui.comboBox_LogLevel->setCurrentIndex(this->ui.comboBox_LogLevel->findData(QVariant(logLevel)));
}

//---------------------------------------------------------------------------
PlusStatus PlusServerLauncherMainWindow::StartRemoteControlServer()
{
  m_RemoteControlServerCallbackCommand = vtkSmartPointer<vtkCallbackCommand>::New();
  m_RemoteControlServerCallbackCommand->SetCallback(PlusServerLauncherMainWindow::OnRemoteControlServerEventReceived);
  m_RemoteControlServerCallbackCommand->SetClientData(this);

  LOG_INFO("Start remote control server at port: " << m_RemoteControlServerPort);
  m_RemoteControlServerLogic = igtlio::LogicPointer::New();
  m_RemoteControlServerLogic->AddObserver(igtlio::Logic::CommandReceivedEvent, m_RemoteControlServerCallbackCommand);
  m_RemoteControlServerLogic->AddObserver(igtlio::Logic::CommandResponseReceivedEvent, m_RemoteControlServerCallbackCommand);
  m_RemoteControlServerSession = m_RemoteControlServerLogic->StartServer(m_RemoteControlServerPort);

  m_RemoteControlServerConnector = m_RemoteControlServerSession->GetConnector();
  m_RemoteControlServerConnector->AddObserver(igtlio::Connector::ConnectedEvent, m_RemoteControlServerCallbackCommand);
  m_RemoteControlServerConnector->AddObserver(igtlio::Connector::DisconnectedEvent, m_RemoteControlServerCallbackCommand);

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
void PlusServerLauncherMainWindow::OnRemoteControlServerEventReceived(vtkObject* caller, unsigned long eventId, void* clientData, void* callData)
{
  PlusServerLauncherMainWindow* self = reinterpret_cast<PlusServerLauncherMainWindow*>(clientData);

  igtlio::LogicPointer logic = dynamic_cast<igtlio::Logic*>(caller);
  igtlio::ConnectorPointer connector = dynamic_cast<igtlio::Connector*>(caller);

  switch (eventId)
  {
  case igtlio::Connector::ConnectedEvent:
    PlusServerLauncherMainWindow::OnConnectEvent(self, connector);
    break;
  case igtlio::Connector::DisconnectedEvent:
    break;
  case igtlio::Logic::CommandReceivedEvent:
    PlusServerLauncherMainWindow::OnCommandReceivedEvent(self, logic);
    break;
  case igtlio::Logic::CommandResponseReceivedEvent:
    break;
  default:
    break;
  }

}

void PlusServerLauncherMainWindow::OnConnectEvent(PlusServerLauncherMainWindow* self, igtlio::ConnectorPointer connector)
{
}


//---------------------------------------------------------------------------
void PlusServerLauncherMainWindow::OnCommandReceivedEvent(PlusServerLauncherMainWindow* self, igtlio::LogicPointer logic)
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
        PlusServerLauncherMainWindow::ParseCommand(self, commandDevice);
      }
      // TODO: should device be deleted after receiving command?
      logic->RemoveDevice(i);
    }
  }
}

//---------------------------------------------------------------------------
void PlusServerLauncherMainWindow::ParseCommand(PlusServerLauncherMainWindow* self, igtlio::CommandDevicePointer commandDevice)
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

    LOG_INFO("Server Start command received");

    vtkSmartPointer<vtkXMLDataElement> logLevelElement = startServerElement->FindNestedElementWithName("LogLevel");
    if (logLevelElement)
    {
      int logLevel = 0;
      if (logLevelElement->GetScalarAttribute("Level", logLevel))
      {
        QMetaObject::invokeMethod(self,
          "SetLogLevel",
          Qt::BlockingQueuedConnection,
          Q_ARG(int, logLevel));
      }
    }

    // Get the filename from the command. The file will be saved to the config file directory with the specified name.
    // If the file already exists, it will be overwritten.
    // If no name is specified, it will be created as a .tmp file in the output directory.
    std::string fileNameAndPath = "";
    vtkSmartPointer<vtkXMLDataElement> fileElement = startServerElement->FindNestedElementWithName("File");
    if (fileElement)
    {
      const char* name = fileElement->GetAttribute("Name");
      if (name)
      {
        std::string path = vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationDirectory();
        fileNameAndPath = path + "/" + name;
        LOG_TRACE("Writing config file to: " << name);
      }
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
        QMetaObject::invokeMethod(self,
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
        QMetaObject::invokeMethod(self->m_DeviceSetSelectorWidget,
          "InvokeConnect",
          Qt::BlockingQueuedConnection);
      }

      std::string serverStarted = "false";
      if (self->m_CurrentServerInstance && self->m_CurrentServerInstance->state() == QProcess::Running)
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
      PlusStatus serverStartSuccess = PLUS_FAIL;
      QMetaObject::invokeMethod(self,
        "ConnectToDevicesByConfigString",
        Qt::BlockingQueuedConnection,
        Q_RETURN_ARG(PlusStatus, serverStartSuccess),
        Q_ARG(std::string, configFileContents),
        Q_ARG(std::string, fileNameAndPath));

      vtkSmartPointer<vtkXMLDataElement> startServerResponse = vtkSmartPointer<vtkXMLDataElement>::New();
      startServerResponse->SetName("StartServer");
      startServerResponse->SetAttribute("Success", serverStartSuccess ? "true" : "false");
      commandResponseElementRoot->AddNestedElement(startServerResponse);
    }
  }
  else if (stopServerElement)
  {
    //TODO: Any info to read for stop command?
    LOG_INFO("Server stop command received");

    bool serverStopSuccess = PLUS_FAIL;
    QMetaObject::invokeMethod(self->m_DeviceSetSelectorWidget,
      "InvokeDisconnect",
      Qt::BlockingQueuedConnection);

    std::string serverStopped = "true";
    if (self->m_CurrentServerInstance && self->m_CurrentServerInstance->state() == QProcess::Running)
    {
      serverStopped = "false";
    }

    vtkSmartPointer<vtkXMLDataElement> stopServerResponse = vtkSmartPointer<vtkXMLDataElement>::New();
    stopServerResponse->SetName("StopServer");
    stopServerResponse->SetAttribute("Success", serverStopped.c_str());
    commandResponseElementRoot->AddNestedElement(stopServerResponse);
  }

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
          if (self->m_CurrentServerInstance)
          {
            status = (self->m_CurrentServerInstance->state() == QProcess::Running) ? "Running" : "Off";
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

  PlusServerLauncherMainWindow::RespondToCommand(self, commandDevice, commandResponseElementRoot);

}

void PlusServerLauncherMainWindow::RespondToCommand(PlusServerLauncherMainWindow* self, igtlio::CommandDevicePointer commandDevice, vtkXMLDataElement* response)
{
  igtlio::CommandConverter::ContentData contentData = commandDevice->GetContent();
  std::string commandName = contentData.name;

  std::stringstream responseStream;
  vtkXMLUtilities::FlattenElement(response, responseStream);
  self->m_RemoteControlServerSession->SendCommandResponse(commandDevice->GetDeviceName(), commandName, responseStream.str());
}