/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

// Local includes
#include "PlusServerLauncherMainWindow.h"
#include "vtkPlusServerLauncherRemoteControl.h"

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

  if (m_RemoteControlServerPort != PlusServerLauncherMainWindow::RemoteControlServerPortDisable)
  {
    if (m_RemoteControlServerPort == PlusServerLauncherMainWindow::RemoteControlServerPortUseDefault)
    {
      m_RemoteControlServerPort = DEFAULT_REMOTE_CONTROL_SERVER_PORT;
    }

    m_LauncherRemoteControl = vtkSmartPointer<vtkPlusServerLauncherRemoteControl>::New();
    m_LauncherRemoteControl->SetServerPort(m_RemoteControlServerPort);
    m_LauncherRemoteControl->SetMainWindow(this);
    m_LauncherRemoteControl->SetDeviceSetSelectorWidget(m_DeviceSetSelectorWidget);
    if (!m_LauncherRemoteControl || !m_LauncherRemoteControl->StartRemoteControlServer())
    {
      LOG_ERROR("Remote control server could not be started!")
    }
  }

}

//-----------------------------------------------------------------------------
PlusServerLauncherMainWindow::~PlusServerLauncherMainWindow()
{
  StopServer(); // deletes m_CurrentServerInstance

  if (m_DeviceSetSelectorWidget != NULL)
  {
    delete m_DeviceSetSelectorWidget;
    m_DeviceSetSelectorWidget = NULL;
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

    if (m_LauncherRemoteControl)
    {
      m_LauncherRemoteControl->SendServerStartupSignal();
    }
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
    if (m_LauncherRemoteControl)
    {
      m_LauncherRemoteControl->SendServerShutdownSignal();
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
void PlusServerLauncherMainWindow::ConnectToDevicesByConfigString(std::string aConfigFileString, std::string aFilename)
{
  if (STRCASECMP("", aFilename.c_str()) == 0)
  {
    PlusCommon::CreateTemporaryFilename(aFilename, vtkPlusConfig::GetInstance()->GetOutputDirectory());
    LOG_INFO("Creating temporary file: " << aFilename);
  }

  // Write contents of server config elements to file
  ofstream file;
  file.open(aFilename);
  file << aConfigFileString;
  file.close();

  // TODO: update UI to show the name of the config file that was passed via string
  vtkPlusConfig::GetInstance()->SetDeviceSetConfigurationFileName(aFilename);

  this->ConnectToDevicesByConfigFile(aFilename);


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

int PlusServerLauncherMainWindow::GetLogLevel()
{
  return this->ui.comboBox_LogLevel->currentData().Int;
}

//----------------------------------------------------------------------------
int PlusServerLauncherMainWindow::GetServerStatus()
{
  if (m_CurrentServerInstance)
  {
    return m_CurrentServerInstance->state();
  }
  else
  {
    return false;
  }
}