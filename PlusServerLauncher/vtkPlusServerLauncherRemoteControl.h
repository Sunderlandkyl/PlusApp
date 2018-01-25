
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

  void SendServerShutdownSignal();

protected:
  vtkPlusServerLauncherRemoteControl();
  virtual ~vtkPlusServerLauncherRemoteControl();

  void StartServerCommand(vtkPlusServerLauncherRemoteControl* self, vtkXMLDataElement* startServerElement, vtkXMLDataElement* commandResponseElementRoot);
  void StopServerCommand(vtkPlusServerLauncherRemoteControl* self, vtkXMLDataElement* startServerElement, vtkXMLDataElement* commandResponseElementRoot);
  void GetCommand(vtkPlusServerLauncherRemoteControl* self, vtkXMLDataElement* startServerElement, vtkXMLDataElement* rootElement);

  static void OnCommandReceivedEvent(vtkPlusServerLauncherRemoteControl* self, igtlio::LogicPointer logic);
  static void ParseCommand(vtkPlusServerLauncherRemoteControl* self, igtlio::CommandDevicePointer);
  static void RespondToCommand(vtkPlusServerLauncherRemoteControl* self, igtlio::CommandDevicePointer commandDevice, vtkXMLDataElement* response);
  static void OnRemoteControlServerEventReceived(vtkObject* caller, unsigned long eventId, void* clientdata, void* calldata);
  static void OnConnectEvent(vtkPlusServerLauncherRemoteControl* self, igtlio::ConnectorPointer connector);

  static void* PlusRemoteThread(vtkMultiThreader::ThreadInfo* data);

  int ServerPort = PlusServerLauncherMainWindow::DEFAULT_REMOTE_CONTROL_SERVER_PORT;

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
  vtkSmartPointer<vtkCallbackCommand>   RemoteControlServerCommandCallback;
  vtkSmartPointer<vtkCallbackCommand>   RemoteControlServerConnectCallback;
  igtlio::LogicPointer                  RemoteControlServerLogic;
  igtlio::ConnectorPointer              RemoteControlServerConnector;
  igtlio::SessionPointer                RemoteControlServerSession;

  vtkSmartPointer<vtkMultiThreader>     Threader;
  std::vector<igtlio::ConnectorPointer> Connections;

  std::pair<bool, bool> RemoteControlActive;
  int CommandId;


private:
  vtkPlusServerLauncherRemoteControl(const vtkPlusServerLauncherRemoteControl&);
  void operator=(const vtkPlusServerLauncherRemoteControl&);

};

#endif // __VTKPLUSSERVERLAUNCHERREMOTECONTROL_H
