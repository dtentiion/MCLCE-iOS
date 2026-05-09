package fourj.documents
{
   import fourj.Base.FJ_Document;
   import fourj.Buttons.FJ_MenuButton_Normal;
   
   public class HelpAndOptionsMenu extends FJ_Document
   {
      
      public var Button1:FJ_MenuButton_Normal;
      
      public var Button2:FJ_MenuButton_Normal;
      
      public var Button3:FJ_MenuButton_Normal;
      
      public var Button4:FJ_MenuButton_Normal;
      
      public var Button5:FJ_MenuButton_Normal;
      
      public var Button6:FJ_MenuButton_Normal;
      
      public var Button7:FJ_MenuButton_Normal;
      
      public function HelpAndOptionsMenu()
      {
         super();
         __setProp_Button1_Scene1_Layer1_0();
         __setProp_Button2_Scene1_Layer1_0();
         __setProp_Button3_Scene1_Layer1_0();
         __setProp_Button4_Scene1_Layer1_0();
         __setProp_Button5_Scene1_Layer1_0();
         __setProp_Button6_Scene1_Layer1_0();
         __setProp_Button7_Scene1_Layer1_0();
         __setTab_Button1_Scene1_Layer1_0();
      }
      
      internal function __setProp_Button1_Scene1_Layer1_0() : *
      {
         try
         {
            Button1["componentInspectorSetting"] = true;
         }
         catch(e:Error)
         {
         }
         Button1.m_objNavDown = "Button2";
         Button1.m_objNavLeft = "";
         Button1.m_objNavRight = "";
         Button1.m_objNavUp = "Button7";
         try
         {
            Button1["componentInspectorSetting"] = false;
         }
         catch(e:Error)
         {
         }
      }
      
      internal function __setProp_Button2_Scene1_Layer1_0() : *
      {
         try
         {
            Button2["componentInspectorSetting"] = true;
         }
         catch(e:Error)
         {
         }
         Button2.m_objNavDown = "Button3";
         Button2.m_objNavLeft = "";
         Button2.m_objNavRight = "";
         Button2.m_objNavUp = "Button1";
         try
         {
            Button2["componentInspectorSetting"] = false;
         }
         catch(e:Error)
         {
         }
      }
      
      internal function __setProp_Button3_Scene1_Layer1_0() : *
      {
         try
         {
            Button3["componentInspectorSetting"] = true;
         }
         catch(e:Error)
         {
         }
         Button3.m_objNavDown = "Button4";
         Button3.m_objNavLeft = "";
         Button3.m_objNavRight = "";
         Button3.m_objNavUp = "Button2";
         try
         {
            Button3["componentInspectorSetting"] = false;
         }
         catch(e:Error)
         {
         }
      }
      
      internal function __setProp_Button4_Scene1_Layer1_0() : *
      {
         try
         {
            Button4["componentInspectorSetting"] = true;
         }
         catch(e:Error)
         {
         }
         Button4.m_objNavDown = "Button5";
         Button4.m_objNavLeft = "";
         Button4.m_objNavRight = "";
         Button4.m_objNavUp = "Button3";
         try
         {
            Button4["componentInspectorSetting"] = false;
         }
         catch(e:Error)
         {
         }
      }
      
      internal function __setProp_Button5_Scene1_Layer1_0() : *
      {
         try
         {
            Button5["componentInspectorSetting"] = true;
         }
         catch(e:Error)
         {
         }
         Button5.m_objNavDown = "Button6";
         Button5.m_objNavLeft = "";
         Button5.m_objNavRight = "";
         Button5.m_objNavUp = "Button4";
         try
         {
            Button5["componentInspectorSetting"] = false;
         }
         catch(e:Error)
         {
         }
      }
      
      internal function __setProp_Button6_Scene1_Layer1_0() : *
      {
         try
         {
            Button6["componentInspectorSetting"] = true;
         }
         catch(e:Error)
         {
         }
         Button6.m_objNavDown = "Button7";
         Button6.m_objNavLeft = "";
         Button6.m_objNavRight = "";
         Button6.m_objNavUp = "Button5";
         try
         {
            Button6["componentInspectorSetting"] = false;
         }
         catch(e:Error)
         {
         }
      }
      
      internal function __setProp_Button7_Scene1_Layer1_0() : *
      {
         try
         {
            Button7["componentInspectorSetting"] = true;
         }
         catch(e:Error)
         {
         }
         Button7.m_objNavDown = "Button1";
         Button7.m_objNavLeft = "";
         Button7.m_objNavRight = "";
         Button7.m_objNavUp = "Button6";
         try
         {
            Button7["componentInspectorSetting"] = false;
         }
         catch(e:Error)
         {
         }
      }
      
      internal function __setTab_Button1_Scene1_Layer1_0() : *
      {
         Button1.tabIndex = 1;
      }
   }
}

