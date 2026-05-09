package fourj.Base
{
   import flash.display.MovieClip;
   import flash.events.Event;
   import flash.events.FocusEvent;
   import fourj.FJ_ScrollArrow;
   
   public class FJ_ButtonList extends FJ_Base
   {
      
      public var m_bUseSmallButtons:Boolean = false;
      
      public var m_iVerticalSpacing:int = 0;
      
      public var m_iMaxButtonsVisible:int = 5;
      
      public var m_sFadePanel:String = "";
      
      private var m_aButtons:Array;
      
      private var m_iActiveButton:int;
      
      private var m_iTopButton:int;
      
      private var m_bWrapAround:Boolean;
      
      private var m_ScrollArrowUp:FJ_ScrollArrow;
      
      private var m_ScrollArrowDown:FJ_ScrollArrow;
      
      private var m_mcFadePanel:MovieClip;
      
      private var m_iTouchScrollYPos:int;
      
      private var m_iTouchedButton:int;
      
      private var m_bTouchScrolled:Boolean;
      
      public function FJ_ButtonList()
      {
         super();
         trace("FJ_ButtonList ctor");
         m_iActiveButton = 0;
         m_iTopButton = 0;
         m_aButtons = new Array();
         m_bWrapAround = false;
         m_iVerticalSpacing = 0;
         m_iMaxButtonsVisible = 5;
         m_iTouchScrollYPos = 0;
         m_iTouchedButton = 0;
         m_bTouchScrolled = false;
         m_ScrollArrowUp = new FJ_ScrollArrow();
         m_ScrollArrowDown = new FJ_ScrollArrow();
         var _loc1_:MovieClip = this.getChildByName("ButtonListBackground") as MovieClip;
         _loc1_.visible = false;
         this.addEventListener(FocusEvent.FOCUS_IN,focusInHandler);
         this.addEventListener(FocusEvent.FOCUS_OUT,focusOutHandler);
      }
      
      public function get iNumButtons() : int
      {
         return m_aButtons.length;
      }
      
      public function Init(param1:int) : void
      {
         InitBase("",param1);
         m_mcFadePanel = this.parent.getChildByName(m_sFadePanel) as MovieClip;
         if(m_mcFadePanel)
         {
            trace("found = " + m_sFadePanel);
         }
         else
         {
            trace("NOT found = " + m_sFadePanel);
         }
      }
      
      private function focusInHandler(param1:FocusEvent) : void
      {
         SetListFocus();
      }
      
      private function focusOutHandler(param1:FocusEvent) : void
      {
         if(m_aButtons.length == 0)
         {
            if(m_mcFadePanel)
            {
               trace("FadeOut");
               m_mcFadePanel.gotoAndPlay("FadeOut");
            }
         }
      }
      
      private function ItemFocusInHandler(param1:FocusEvent) : void
      {
         var _loc2_:Boolean = FJ_Button(param1.target).iListId < m_iActiveButton ? true : false;
         m_iActiveButton = FJ_Button(param1.target).iListId;
         ScrollList(_loc2_);
      }
      
      public function SetListFocus() : void
      {
         if(stage.focus != this)
         {
            return;
         }
         trace("SetListFocus [" + this.name + "]");
         if(m_aButtons.length > 0)
         {
            stage.focus = m_aButtons[m_iActiveButton];
            dispatchEvent(new Event(FJ_Document.FJ_EVENT_FOCUS_CHANGE_INIT,true));
         }
         if(m_mcFadePanel)
         {
            trace("FadeIn");
            m_mcFadePanel.gotoAndPlay("FadeIn");
         }
      }
      
      public function HandlePageUpDownNavigation(param1:Boolean) : void
      {
         if(param1)
         {
            if(m_iActiveButton > m_iTopButton)
            {
               HighlightItem(m_iTopButton);
            }
            else
            {
               HighlightItem(m_iActiveButton - m_iMaxButtonsVisible > 0 ? int(m_iActiveButton - m_iMaxButtonsVisible) : 0);
            }
         }
         else if(m_iActiveButton < m_iTopButton + m_iMaxButtonsVisible - 1)
         {
            HighlightItem(m_iTopButton + m_iMaxButtonsVisible - 1);
         }
         else
         {
            HighlightItem(m_iActiveButton + m_iMaxButtonsVisible < m_aButtons.length ? int(m_iActiveButton + m_iMaxButtonsVisible) : int(m_aButtons.length - 1));
         }
      }
      
      public function LoseListFocus() : void
      {
         if(stage.focus is FJ_Button)
         {
            if(Boolean(FJ_Button(stage.focus).objListParent) && FJ_Button(stage.focus).objListParent != this)
            {
               trace("LoseListFocus [" + this.name + "]");
               if(m_mcFadePanel)
               {
                  trace("FadeOut");
                  m_mcFadePanel.gotoAndPlay("FadeOut");
               }
            }
            else if(!FJ_Button(stage.focus).objListParent)
            {
               if(m_mcFadePanel)
               {
                  trace("FadeOut");
                  m_mcFadePanel.gotoAndPlay("FadeOut");
               }
            }
         }
         else if(stage.focus is FJ_ButtonList)
         {
            if(stage.focus != this)
            {
               if(m_mcFadePanel)
               {
                  trace("FadeOut");
                  m_mcFadePanel.gotoAndPlay("FadeOut");
               }
            }
         }
      }
      
      private function ScrollList(param1:Boolean, param2:Boolean = false) : void
      {
         if(!param2)
         {
            if(!param1 && m_iActiveButton >= m_iTopButton + m_iMaxButtonsVisible)
            {
               m_iTopButton = m_iActiveButton - (m_iMaxButtonsVisible - 1);
               if(m_ScrollArrowDown)
               {
                  FJ_ScrollArrow(m_ScrollArrowDown).ChangeState(FJ_ScrollArrow.ARROW_B_ANIMATING);
               }
            }
            else if(param1 && m_iActiveButton < m_iTopButton)
            {
               m_iTopButton = m_iActiveButton;
               if(m_ScrollArrowUp)
               {
                  FJ_ScrollArrow(m_ScrollArrowUp).ChangeState(FJ_ScrollArrow.ARROW_A_ANIMATING);
               }
            }
         }
         var _loc3_:Number = 0;
         var _loc4_:int = 0;
         while(_loc4_ < m_aButtons.length)
         {
            _loc3_ = (m_aButtons[_loc4_].height + m_iVerticalSpacing) * (_loc4_ - m_iTopButton);
            m_aButtons[_loc4_].y = this.y + _loc3_;
            if(_loc4_ < m_iTopButton || _loc4_ >= m_iTopButton + m_iMaxButtonsVisible)
            {
               m_aButtons[_loc4_].visible = false;
            }
            else
            {
               m_aButtons[_loc4_].visible = true;
            }
            _loc4_++;
         }
         if(!param2)
         {
            HandleScrollListArrows(false);
         }
      }
      
      private function HandleScrollListArrows(param1:Boolean) : void
      {
         if(param1 && m_aButtons.length >= m_iMaxButtonsVisible && !m_ScrollArrowUp.parent)
         {
            parent.addChild(m_ScrollArrowUp);
            parent.addChild(m_ScrollArrowDown);
            m_ScrollArrowUp.x = Math.round(m_aButtons[m_aButtons.length - 1].x + m_aButtons[m_aButtons.length - 1].width - m_ScrollArrowUp.width * 1.5);
            m_ScrollArrowUp.y = Math.round(m_aButtons[m_aButtons.length - 1].y + m_aButtons[m_aButtons.length - 1].height + m_ScrollArrowUp.height * 0.75);
            m_ScrollArrowDown.x = Math.round(m_aButtons[m_aButtons.length - 1].x + m_aButtons[m_aButtons.length - 1].width - m_ScrollArrowUp.width / 2);
            m_ScrollArrowDown.y = m_ScrollArrowUp.y;
            FJ_ScrollArrow(m_ScrollArrowUp).ChangeState(FJ_ScrollArrow.ARROW_A_VISIBLE);
            FJ_ScrollArrow(m_ScrollArrowDown).ChangeState(FJ_ScrollArrow.ARROW_B_VISIBLE);
         }
         if(m_iTopButton > 0)
         {
            FJ_ScrollArrow(m_ScrollArrowUp).visible = true;
         }
         else
         {
            FJ_ScrollArrow(m_ScrollArrowUp).visible = false;
         }
         if(m_iTopButton < m_aButtons.length - m_iMaxButtonsVisible)
         {
            FJ_ScrollArrow(m_ScrollArrowDown).visible = true;
         }
         else
         {
            FJ_ScrollArrow(m_ScrollArrowDown).visible = false;
         }
      }
      
      protected function addNewListItem(param1:FJ_Button, param2:String, param3:int) : void
      {
         var _loc4_:int = int(m_aButtons.length);
         param1.SetId(param3);
         param1.iListId = _loc4_;
         param1.objListParent = this;
         param1.width = this.width;
         param1.x = this.x + Math.round((this.width - param1.width) / 2);
         m_aButtons.push(param1);
         param1.m_objNavLeft = this.m_objNavLeft;
         param1.m_objNavRight = this.m_objNavRight;
         param1.m_objNavDown = this.m_objNavDown;
         if(_loc4_ > 0)
         {
            param1.m_objNavUp = FJ_Button(m_aButtons[_loc4_ - 1]).name;
            FJ_Button(m_aButtons[_loc4_ - 1]).m_objNavDown = param1.name;
         }
         else
         {
            param1.m_objNavUp = this.m_objNavUp;
         }
         param1.addEventListener(FocusEvent.FOCUS_IN,ItemFocusInHandler);
         parent.addChild(param1);
         if(_loc4_ == m_iActiveButton)
         {
            SetListFocus();
         }
         ScrollList(false,true);
         HandleScrollListArrows(true);
      }
      
      public function removeAllItems() : void
      {
         var _loc1_:Boolean = false;
         var _loc2_:int = 0;
         while(_loc2_ < m_aButtons.length)
         {
            if(stage.focus == m_aButtons[_loc2_])
            {
               _loc1_ = true;
            }
            parent.removeChild(m_aButtons[_loc2_]);
            _loc2_++;
         }
         m_aButtons = [];
         m_iActiveButton = 0;
         m_iTopButton = 0;
         HandleScrollListArrows(false);
         if(_loc1_)
         {
            trace("removeAllItems - TransferFocus to list");
            stage.focus = this;
         }
      }
      
      public function SetButtonLabel(param1:int, param2:String) : void
      {
         if(param1 >= m_aButtons.length)
         {
            trace("404 - List Item not found");
            return;
         }
         FJ_Button(m_aButtons[param1]).SetLabel(param2);
      }
      
      public function SetTouchFocus(param1:int, param2:int, param3:Boolean) : void
      {
         var _loc4_:int = 0;
         var _loc5_:int = 0;
         var _loc6_:int = 0;
         if(!param3)
         {
            _loc4_ = 0;
            while(_loc4_ < m_aButtons.length)
            {
               if(param2 > m_aButtons[_loc4_].y && param2 < m_aButtons[_loc4_].y + m_aButtons[_loc4_].height)
               {
                  m_iTouchedButton = _loc4_;
                  m_bTouchScrolled = false;
                  HighlightItem(_loc4_);
                  break;
               }
               _loc4_++;
            }
            m_iTouchScrollYPos = param2;
         }
         else if(m_aButtons.length > 0 && m_aButtons.length > m_iMaxButtonsVisible)
         {
            _loc5_ = m_iActiveButton;
            _loc6_ = 0;
            if(param2 < m_iTouchScrollYPos)
            {
               _loc6_ = (m_iTouchScrollYPos - param2) / (m_aButtons[0].height + m_iVerticalSpacing);
               if(_loc6_ > 0)
               {
                  m_bTouchScrolled = true;
                  m_iTopButton += _loc6_;
                  if(m_iTopButton + m_iMaxButtonsVisible >= m_aButtons.length)
                  {
                     m_iTopButton = m_aButtons.length - m_iMaxButtonsVisible;
                  }
                  ScrollList(false,false);
                  m_iTouchScrollYPos = param2;
                  if(m_ScrollArrowDown)
                  {
                     FJ_ScrollArrow(m_ScrollArrowDown).ChangeState(FJ_ScrollArrow.ARROW_B_ANIMATING);
                  }
                  if(m_iActiveButton < m_iTopButton)
                  {
                     HighlightItem(m_iTopButton);
                  }
               }
            }
            else if(param2 > m_iTouchScrollYPos)
            {
               _loc6_ = (param2 - m_iTouchScrollYPos) / (m_aButtons[0].height + m_iVerticalSpacing);
               if(_loc6_ > 0)
               {
                  m_bTouchScrolled = true;
                  m_iTopButton -= _loc6_;
                  if(m_iTopButton < 0)
                  {
                     m_iTopButton = 0;
                  }
                  ScrollList(true,false);
                  m_iTouchScrollYPos = param2;
                  if(m_ScrollArrowUp)
                  {
                     FJ_ScrollArrow(m_ScrollArrowUp).ChangeState(FJ_ScrollArrow.ARROW_A_ANIMATING);
                  }
                  if(m_iActiveButton > m_iTopButton + m_iMaxButtonsVisible - 1)
                  {
                     HighlightItem(m_iTopButton + m_iMaxButtonsVisible - 1);
                  }
               }
            }
         }
      }
      
      public function CanTouchTrigger(param1:int, param2:int) : Boolean
      {
         var _loc3_:int = 0;
         while(_loc3_ < m_aButtons.length)
         {
            if(param2 > m_aButtons[_loc3_].y && param2 < m_aButtons[_loc3_].y + m_aButtons[_loc3_].height)
            {
               if(!m_bTouchScrolled && m_iTouchedButton == _loc3_)
               {
                  return true;
               }
            }
            _loc3_++;
         }
         return false;
      }
      
      public function HighlightItem(param1:int) : void
      {
         if(param1 >= m_aButtons.length)
         {
            trace("404 - List Item not found");
            return;
         }
         var _loc2_:Boolean = param1 < m_iActiveButton ? true : false;
         m_iActiveButton = param1;
         stage.focus = m_aButtons[m_iActiveButton];
         dispatchEvent(new Event(FJ_Document.FJ_EVENT_FOCUS_CHANGE_INIT,true));
         ScrollList(_loc2_,false);
      }
      
      public function RemoveItem(param1:int) : void
      {
         var _loc3_:Boolean = false;
         if(param1 >= m_aButtons.length)
         {
            trace("404 - List Item not found");
            return;
         }
         FixNavigation(m_aButtons[param1]);
         parent.removeChild(m_aButtons[param1]);
         m_aButtons.splice(param1,1);
         var _loc2_:int = 0;
         while(_loc2_ < m_aButtons.length)
         {
            FJ_Button(m_aButtons[_loc2_]).iListId = _loc2_;
            _loc2_++;
         }
         if(m_aButtons.length > 0)
         {
            _loc3_ = param1 < m_iActiveButton ? true : false;
            if(_loc3_)
            {
               if(m_iTopButton > 0)
               {
                  --m_iTopButton;
               }
            }
            else if(m_iTopButton > 0 && m_iTopButton + m_iMaxButtonsVisible > m_aButtons.length)
            {
               --m_iTopButton;
            }
            ScrollList(true,true);
         }
         if(param1 <= m_iActiveButton)
         {
            if(m_iActiveButton > 0)
            {
               --m_iActiveButton;
            }
            stage.focus = m_aButtons[m_iActiveButton];
         }
         HandleScrollListArrows(true);
      }
      
      private function FixNavigation(param1:MovieClip) : void
      {
         var _loc2_:MovieClip = null;
         var _loc3_:MovieClip = null;
         if(FJ_Base(param1).m_objNavDown)
         {
            _loc2_ = parent.getChildByName(FJ_Base(param1).m_objNavDown) as MovieClip;
            if(_loc2_)
            {
               FJ_Base(_loc2_).m_objNavUp = FJ_Base(param1).m_objNavUp;
            }
         }
         if(FJ_Base(param1).m_objNavUp)
         {
            _loc3_ = parent.getChildByName(FJ_Base(param1).m_objNavUp) as MovieClip;
            if(_loc3_)
            {
               FJ_Base(_loc3_).m_objNavDown = FJ_Base(param1).m_objNavDown;
            }
         }
      }
      
      public function ReturnItem(param1:int) : FJ_Button
      {
         if(param1 >= m_aButtons.length)
         {
            trace("404 - List Item not found");
            return null;
         }
         return m_aButtons[param1];
      }
   }
}

