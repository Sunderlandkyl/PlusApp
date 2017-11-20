
#ifndef __VTKPLUSSERVERLAUNCHERREMOTECONTROL_H
#define __VTKPLUSSERVERLAUNCHERREMOTECONTROL_H

#include "PlusConfigure.h"

// VTK includes
#include <vtkObject.h>
#include <vtkSmartPointer.h>
#include <vtkSetGet.h>

// OpenIGTLinkIO includes
#include <igtlioLogic.h>
#include <igtlioConnector.h>
#include <igtlioSession.h>

// PlusApp includes
#include <PlusServerLauncherMainWindow.h>

// PlusLib includes

class vtkPlusServerLauncherRemoteControl : public vtkObject
{

public:
  static vtkPlusServerLauncherRemoteControl* New();
  vtkTypeMacro(vtkPlusServerLauncherRemoteControl, vtkObject);
  virtual void PrintSelf(ostream& os, vtkIndent indent) VTK_OVERRIDE;

  PlusStatus StartRemoteControlServer();
  PlusStatus StopRemoteControlServer();
  void CreateNewConnector();

  void SendServerStartupSignal(const char* filename = "");
  void SendServerShutdownSignal();

protected:
  vtkPlusServerLauncherRemoteControl();
  virtual ~vtkPlusServerLauncherRemoteControl();

  void StartServerCommand(vtkPlusServerLauncherRemoteControl* self, vtkXMLDataElement* startServerCommandElement, igtlio::CommandDevicePointer commandDevice);
  void StopServerCommand(vtkPlusServerLauncherRemoteControl* self, vtkXMLDataElement* stopServerCommandElement, igtlio::CommandDevicePointer commandDevice);
  void GetServerInfoCommand(vtkPlusServerLauncherRemoteControl* self, vtkXMLDataElement* getServerInfoCommandElement, igtlio::CommandDevicePointer commandDevice);

  std::string GetOutgoingPortsFromConfigFile(vtkXMLDataElement* configFileElement);

  static void OnCommandReceivedEvent(vtkPlusServerLauncherRemoteControl* self, igtlio::LogicPointer logic);
  static void RespondToCommand(vtkPlusServerLauncherRemoteControl* self, igtlio::CommandDevicePointer commandDevice, vtkXMLDataElement* response);
  static void OnRemoteControlServerEventReceived(vtkObject* caller, unsigned long eventId, void* clientdata, void* calldata);
  static void OnConnectEvent(vtkPlusServerLauncherRemoteControl* self, igtlio::ConnectorPointer connector);
  static void OnDisconnectEvent(vtkPlusServerLauncherRemoteControl* self, igtlio::ConnectorPointer connector);
  static void OnLogEvent(vtkObject* caller, unsigned long eventId, void* clientData, void* callData);

  static void* PlusRemoteThread(vtkMultiThreader::ThreadInfo* data);

  std::string GetCommandName();

public:
  vtkGetMacro(ServerPort, int);
  vtkSetMacro(ServerPort, int);

  vtkSetMacro(DeviceSetSelectorWidget, QPlusDeviceSetSelectorWidget*);
  vtkSetMacro(MainWindow, PlusServerLauncherMainWindow*);

protected:
  QPlusDeviceSetSelectorWidget* DeviceSetSelectorWidget;
  PlusServerLauncherMainWindow* MainWindow;

  //TODO: rename all qt variables
  /*! OpenIGTLink server that allows remote control of launcher (start/stop a PlusServer process, etc) */
  vtkSmartPointer<vtkCallbackCommand>   RemoteControlServerCallbackCommand;
  igtlio::LogicPointer                  RemoteControlServerLogic;
  igtlio::ConnectorPointer              RemoteControlServerConnector;
  igtlio::SessionPointer                RemoteControlServerSession;
  vtkSmartPointer<vtkCallbackCommand>   LogMessageCallbackCommand;

  vtkSmartPointer<vtkMultiThreader>     Threader;
  std::vector<igtlio::ConnectorPointer> Connections;
  std::vector<igtlio::SessionPointer> Sessions;
  std::vector<igtlio::LogicPointer> Logics;

  std::pair<bool, bool> RemoteControlActive;
  int CommandId;

  int ServerPort = PlusServerLauncherMainWindow::DEFAULT_REMOTE_CONTROL_SERVER_PORT;

private:
  vtkPlusServerLauncherRemoteControl(const vtkPlusServerLauncherRemoteControl&);
  void operator=(const vtkPlusServerLauncherRemoteControl&);

};

#endif // __VTKPLUSSERVERLAUNCHERREMOTECONTROL_H
