package fourj.Base
{
   import flash.display.MovieClip;
   import flash.events.Event;
   import flash.events.FocusEvent;
   import flash.events.KeyboardEvent;
   import flash.ui.Keyboard;
   
   public class FJ_Button extends FJ_Base
   {
      
      private static var m_bButtonPressed:Boolean;
      
      public static const FJ_EVENT_BUTTON_PRESS:String = "FJ_Button_Down";
      
      public static const BUTTON_FIRST_SELECTED:int = 0;
      
      public static const BUTTON_SELECTED:int = 1;
      
      public static const BUTTON_UNSELECTED:int = 2;
      
      public static const BUTTON_PRESSED:int = 3;
      
      private static const BUTTON_STATES:Array = new Array(new Array("Active_First_Selected","Inactive_Selected"),new Array("Active_Selected","Inactive_Selected"),new Array("Active_Unselected","Inactive_Unselected"),new Array("Active_Pressed","Inactive_Selected"));
      
      private var m_bShowOutline:Boolean;
      
      private var m_bEnabled:Boolean;
      
      private var m_iCurrentState:int;
      
      private var m_objListParent:Object;
      
      private var m_fKeyDownDelegate:Function;
      
      private var m_iListId:int;
      
      private var m_bHideNonInitialised:Boolean;
      
      public function FJ_Button()
      {
         super();
         m_bButtonPressed = false;
         m_bShowOutline = false;
         m_bEnabled = true;
         m_iListId = 0;
         m_fKeyDownDelegate = null;
         this.buttonMode = true;
         this.tabEnabled = true;
         ChangeState(BUTTON_UNSELECTED);
         ToggleOutline(false);
         this.addEventListener(FocusEvent.FOCUS_IN,focusInHandler);
         this.addEventListener(FocusEvent.FOCUS_OUT,focusOutHandler);
         this.addEventListener(KeyboardEvent.KEY_DOWN,keyDownHandler);
         this.addEventListener(KeyboardEvent.KEY_UP,keyUpHandler);
      }
      
      public function get ButtonPressed() : Boolean
      {
         return m_bButtonPressed;
      }
      
      public function set ButtonPressed(param1:Boolean) : void
      {
         m_bButtonPressed = param1;
      }
      
      public function get ShowOutline() : Boolean
      {
         return m_bShowOutline;
      }
      
      public function get Enabled() : Boolean
      {
         return m_bEnabled;
      }
      
      public function get CurrentState() : int
      {
         return m_iCurrentState;
      }
      
      public function get objListParent() : Object
      {
         return m_objListParent;
      }
      
      public function set objListParent(param1:Object) : void
      {
         m_objListParent = param1;
      }
      
      public function get iListId() : int
      {
         return m_iListId;
      }
      
      public function set iListId(param1:int) : void
      {
         m_iListId = param1;
      }
      
      public function HideUntilInit() : *
      {
         this.visible = false;
         m_bHideNonInitialised = true;
      }
      
      public function Init(param1:String, param2:int) : void
      {
         if(m_bHideNonInitialised)
         {
            this.visible = true;
            m_bHideNonInitialised = false;
         }
         InitBase(param1,param2);
         this.addEventListener("UpdateLabel",HandleUpdateLabel);
      }
      
      public function SetKeyDownDelegate(param1:Function) : void
      {
         m_fKeyDownDelegate = param1;
      }
      
      private function focusInHandler(param1:FocusEvent) : void
      {
         ChangeState(BUTTON_SELECTED);
      }
      
      private function focusOutHandler(param1:FocusEvent) : void
      {
         ChangeState(BUTTON_UNSELECTED);
      }
      
      private function keyDownHandler(param1:KeyboardEvent) : void
      {
         if(!m_bEnabled)
         {
            return;
         }
         if(!m_bButtonPressed && param1.keyCode == Keyboard.ENTER)
         {
            ChangeState(BUTTON_PRESSED);
            m_bButtonPressed = true;
            dispatchEvent(new Event(FJ_EVENT_BUTTON_PRESS,true));
            if(m_fKeyDownDelegate != null)
            {
               m_fKeyDownDelegate(m_iId);
            }
         }
      }
      
      private function keyUpHandler(param1:KeyboardEvent) : void
      {
         if(m_bButtonPressed && param1.keyCode == Keyboard.ENTER)
         {
            m_bButtonPressed = false;
         }
      }
      
      public function ChangeState(param1:int) : void
      {
         var _loc2_:int = m_bEnabled ? 0 : 1;
         if((param1 == BUTTON_FIRST_SELECTED || param1 == BUTTON_PRESSED) && m_bEnabled)
         {
            this.gotoAndPlay(BUTTON_STATES[param1][_loc2_]);
         }
         else
         {
            this.gotoAndStop(BUTTON_STATES[param1][_loc2_]);
         }
         m_iCurrentState = param1;
      }
      
      public function HandleUpdateLabel(param1:Event) : void
      {
         UpdateLabel();
      }
      
      private function UpdateLabel() : void
      {
         this.SetLabel(m_sText);
      }
      
      override public function SetLabel(param1:String) : void
      {
         super.SetLabel(param1);
      }
      
      public function ToggleOutline(param1:Boolean) : void
      {
         var _loc2_:MovieClip = this.getChildByName("Outline") as MovieClip;
         if(_loc2_)
         {
            if(param1)
            {
               _loc2_.gotoAndPlay("Outline_Enabled");
            }
            else
            {
               _loc2_.gotoAndPlay("Outline_Disabled");
            }
         }
         m_bShowOutline = param1;
      }
      
      public function EnableButton(param1:Boolean) : void
      {
         if(m_bEnabled != param1)
         {
            m_bEnabled = param1;
            ChangeState(m_iCurrentState);
         }
      }
      
      public function LostFocus() : void
      {
         if(Boolean(m_objListParent) && m_objListParent is FJ_ButtonList)
         {
            FJ_ButtonList(m_objListParent).LoseListFocus();
         }
      }
   }
}

