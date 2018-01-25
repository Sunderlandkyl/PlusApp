
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
  void SendServerShutdownSignal();

protected:
  vtkPlusServerLauncherRemoteControl();
  virtual ~vtkPlusServerLauncherRemoteControl();

  int ServerPort = PlusServerLauncherMainWindow::DEFAULT_REMOTE_CONTROL_SERVER_PORT;

  QPlusDeviceSetSelectorWidget* DeviceSetSelectorWidget;
  PlusServerLauncherMainWindow* MainWindow;

  //TODO: rename all qt variables
  /*! OpenIGTLink server that allows remote control of launcher (start/stop a PlusServer process, etc) */
  vtkSmartPointer<vtkCallbackCommand>   RemoteControlServerCallbackCommand;
  igtlio::LogicPointer                  RemoteControlServerLogic;
  igtlio::ConnectorPointer              RemoteControlServerConnector;
  igtlio::SessionPointer                RemoteControlServerSession;
  vtkSmartPointer<vtkMultiThreader>     Threader;
  std::vector<igtlio::ConnectorPointer> Connections;

  /*! PlusServer instance that is responsible for all data collection and network transfer */
  QProcess*                             CurrentServerInstance;

  std::pair<bool, bool> RemoteControlActive;
  int CommandId;

  static void OnCommandReceivedEvent(vtkPlusServerLauncherRemoteControl* self, igtlio::LogicPointer logic);
  static void ParseCommand(vtkPlusServerLauncherRemoteControl* self, igtlio::CommandDevicePointer);
  static void RespondToCommand(vtkPlusServerLauncherRemoteControl* self, igtlio::CommandDevicePointer commandDevice, vtkXMLDataElement* response);
  static void OnRemoteControlServerEventReceived(vtkObject* caller, unsigned long eventId, void* clientdata, void* calldata);
  static void OnConnectEvent(vtkPlusServerLauncherRemoteControl* self, igtlio::ConnectorPointer connector);

  static void* PlusRemoteThread(vtkMultiThreader::ThreadInfo* data);

public:
  vtkGetMacro(ServerPort, int);
  vtkSetMacro(ServerPort, int);

  vtkSetMacro(DeviceSetSelectorWidget, QPlusDeviceSetSelectorWidget*);
  vtkSetMacro(MainWindow, PlusServerLauncherMainWindow*);

private:
  vtkPlusServerLauncherRemoteControl(const vtkPlusServerLauncherRemoteControl&);
  void operator=(const vtkPlusServerLauncherRemoteControl&);

};

#endif // __VTKPLUSSERVERLAUNCHERREMOTECONTROL_H