package fourj.Base
{
   import flash.display.MovieClip;
   import flash.text.TextField;
   import flash.text.TextFieldAutoSize;
   import flash.text.TextFormat;
   
   public class FJ_Base extends MovieClip
   {
      
      public static const ALIGN_NONE:* = 0;
      
      public static const ALIGN_LEFT:* = 1;
      
      public static const ALIGN_CENTER:* = 2;
      
      public static const ALIGN_RIGHT:* = 3;
      
      public static const ALIGN_VERTICALCENTER:* = 4;
      
      public var m_objNavUp:String;
      
      public var m_objNavDown:String;
      
      public var m_objNavLeft:String;
      
      public var m_objNavRight:String;
      
      protected var m_iId:int;
      
      protected var m_iTextFieldAlign:int;
      
      protected var m_bAutoSize:Boolean;
      
      protected var m_bMultiLine:Boolean;
      
      protected var m_bWordWrap:Boolean;
      
      protected var m_bUseHtmlText:Boolean;
      
      protected var m_TextFormat:TextFormat;
      
      protected var m_sText:String;
      
      protected var m_nTextXOffset:Number;
      
      protected var m_nWidth:Number;
      
      protected var m_nHeight:Number;
      
      protected var m_bInitialised:Boolean;
      
      protected var m_bResized:Boolean;
      
      protected var m_bLabelTooWide:Boolean;
      
      protected var m_iNewWidth:int;
      
      protected var m_iSafeLabelWidth:int;
      
      protected var m_bUseEllipsis:Boolean;
      
      protected var m_iEllipsisRightEdgeOffset:int;
      
      public function FJ_Base()
      {
         super();
         m_iId = -1;
         m_iTextFieldAlign = ALIGN_NONE;
         m_bAutoSize = true;
         m_bMultiLine = false;
         m_bWordWrap = false;
         m_bUseHtmlText = false;
         m_nTextXOffset = 0;
         m_nWidth = -1;
         m_nHeight = -1;
         m_bInitialised = false;
         m_bResized = false;
         m_bLabelTooWide = false;
         m_iNewWidth = this.width;
         m_bUseEllipsis = false;
         m_iEllipsisRightEdgeOffset = 0;
         CorrectPixelPosition();
      }
      
      public function get id() : int
      {
         return m_iId;
      }
      
      public function get bLabelTooWide() : Boolean
      {
         return m_bLabelTooWide;
      }
      
      public function get iNewWidth() : int
      {
         return m_iNewWidth;
      }
      
      public function get iOldWidth() : int
      {
         return width;
      }
      
      protected function InitBase(param1:String, param2:int = -1) : void
      {
         m_iId = param2;
         m_bInitialised = true;
         SetLabel(param1);
      }
      
      public function SetId(param1:int) : void
      {
         m_iId = param1;
      }
      
      public function CheckLabelWidth(param1:String) : void
      {
         var _loc2_:TextField = _loc2_ = GetTextField();
         if(param1 != null)
         {
            _loc2_.text = param1;
            GetLabelTooWide(_loc2_);
         }
      }
      
      public function CheckLabelWidths(... rest) : void
      {
         var _loc2_:int = 0;
         if(Boolean(rest) && rest.length > 0)
         {
            _loc2_ = 0;
            while(_loc2_ < rest.length)
            {
               CheckLabelWidth(rest[_loc2_]);
               _loc2_++;
            }
         }
      }
      
      private function CorrectPixelPosition() : void
      {
         if(this.x % 1 != 0)
         {
            trace("CorrectPixelPosition X - " + Number(this.x) + " to " + Math.round(this.x) + " - " + this.name);
            this.x = Math.round(this.x);
         }
         if(this.y % 1 != 0)
         {
            trace("CorrectPixelPosition Y - " + Number(this.y) + " to " + Math.round(this.y) + " - " + this.name);
            this.y = Math.round(this.y);
         }
      }
      
      public function SetLabelAlignment(param1:int) : void
      {
         m_iTextFieldAlign = param1;
      }
      
      public function GetTextField() : TextField
      {
         var _loc1_:TextField = null;
         var _loc2_:MovieClip = null;
         var _loc3_:MovieClip = null;
         if(this is FJ_Label)
         {
            _loc3_ = this.getChildAt(0) as MovieClip;
         }
         else
         {
            _loc2_ = this.getChildByName("FJ_TextContainer") as MovieClip;
            if(_loc2_)
            {
               if(_loc2_.numChildren)
               {
                  _loc3_ = _loc2_.getChildAt(0) as MovieClip;
               }
            }
         }
         if(_loc3_)
         {
            _loc1_ = _loc3_.getChildAt(0) as TextField;
         }
         return _loc1_;
      }
      
      public function SetLabel(param1:String) : void
      {
         var _loc2_:TextField = null;
         var _loc3_:MovieClip = null;
         m_sText = param1;
         if(m_bLabelTooWide)
         {
            trace("label too wide! Fix via SetNewObjectWidth first!");
            return;
         }
         _loc3_ = this.getChildByName("FJ_TextContainer") as MovieClip;
         _loc2_ = GetTextField();
         if(!_loc2_)
         {
            trace("This component doesn\'t seem to have a Text Field");
            return;
         }
         if(!m_bInitialised && param1 != null)
         {
            _loc2_.text = "Not Initialised!";
            return;
         }
         if(m_nWidth == -1)
         {
            m_nWidth = width + this.getRect(this).x * 2;
         }
         if(m_nHeight == -1)
         {
            m_nHeight = height + this.getRect(this).y * 2;
         }
         _loc2_.multiline = m_bMultiLine;
         _loc2_.wordWrap = m_bWordWrap;
         if(param1 != null)
         {
            if(m_bUseHtmlText)
            {
               _loc2_.htmlText = param1;
            }
            else
            {
               _loc2_.text = param1;
            }
         }
         if(_loc3_)
         {
            _loc3_.scaleX = 1;
            _loc3_.scaleY = 1;
         }
         _loc2_.scaleX = 1 / scaleX;
         _loc2_.scaleY = 1 / scaleY;
         if(m_bUseEllipsis)
         {
            DoEllipsisCheck(_loc3_,_loc2_);
         }
         else if(!m_bResized)
         {
            GetLabelTooWide(_loc2_);
         }
         if(m_bLabelTooWide)
         {
            _loc2_.autoSize = TextFieldAutoSize.NONE;
            _loc2_.width = m_iSafeLabelWidth;
            _loc2_.text = "[...] " + _loc2_.text;
         }
         if(!m_bLabelTooWide && m_bAutoSize)
         {
            _loc2_.autoSize = TextFieldAutoSize.LEFT;
         }
         if(m_bWordWrap)
         {
            _loc2_.width = m_nWidth;
         }
         if(m_iTextFieldAlign == ALIGN_CENTER)
         {
            if(_loc3_)
            {
               _loc3_.x = 0;
               _loc3_.y = 0;
            }
            _loc2_.x = Math.round((m_nWidth / scaleX - _loc2_.width) / 2 + m_nTextXOffset);
            _loc2_.y = Math.round((m_nHeight / scaleY - _loc2_.height) / 2);
         }
         else if(m_iTextFieldAlign == ALIGN_RIGHT)
         {
            if(_loc3_)
            {
               _loc3_.x = 0;
               _loc3_.y = 0;
            }
            _loc2_.x = Math.round(m_nWidth / scaleX - _loc2_.width);
            _loc2_.y = Math.round((m_nHeight / scaleY - _loc2_.height) / 2);
         }
         else if(m_iTextFieldAlign == ALIGN_LEFT)
         {
            if(_loc3_)
            {
               _loc3_.x = 0;
               _loc3_.y = 0;
            }
            _loc2_.x = 0;
            _loc2_.y = Math.round((m_nHeight / scaleY - _loc2_.height) / 2);
         }
         else if(m_iTextFieldAlign == ALIGN_VERTICALCENTER)
         {
            if(_loc3_)
            {
               _loc3_.y = 0;
            }
            _loc2_.x = Math.round(_loc2_.x);
            _loc2_.y = Math.round((m_nHeight / scaleY - _loc2_.height) / 2);
         }
         else if(m_bAutoSize)
         {
            _loc2_.x = 0;
            _loc2_.y = 0;
         }
      }
      
      public function GetLabel() : String
      {
         return m_sText;
      }
      
      protected function SetTextOnly(param1:String) : void
      {
         var _loc2_:TextField = null;
         _loc2_ = GetTextField();
         _loc2_.text = param1;
      }
      
      protected function DoEllipsisCheck(param1:MovieClip, param2:TextField) : void
      {
         var _loc6_:int = 0;
         var _loc3_:int = 5;
         var _loc4_:int = 0;
         var _loc5_:int = 0;
         if(param1)
         {
            _loc4_ = param1.x * this.scaleX;
            _loc5_ = this.width;
         }
         else
         {
            _loc5_ = this.width * this.scaleX;
         }
         if(m_iEllipsisRightEdgeOffset > 0)
         {
            _loc5_ = m_iEllipsisRightEdgeOffset;
            _loc3_ = 0;
         }
         if(_loc4_ + param2.textWidth + _loc3_ > _loc5_)
         {
            _loc6_ = _loc5_ - _loc4_ - _loc3_;
            do
            {
               param2.text = param2.text.slice(0,param2.text.length - 1);
               trace(this.name + " tfTextField.textWidth = " + param2.textWidth + " > " + _loc6_);
            }
            while(param2.textWidth > _loc6_);
            param2.text = param2.text.slice(0,param2.text.length - 3);
            m_sText = param2.text + "...";
            param2.text = m_sText;
         }
      }
      
      protected function GetLabelTooWide(param1:TextField) : void
      {
         var _loc2_:int = param1.textWidth > width ? int(param1.textWidth) : int(width);
         m_iNewWidth = _loc2_ > m_iNewWidth ? _loc2_ : m_iNewWidth;
         if(!m_bLabelTooWide && param1.textWidth > this.width)
         {
            m_bLabelTooWide = true;
            m_iSafeLabelWidth = m_nWidth * 0.8;
            if(this.name.indexOf("console") == -1)
            {
               trace(this.name + " tfTextField.textWidth exceeding parent width!");
            }
         }
      }
      
      public function SetNewObjectWidth(param1:int) : void
      {
         if(param1 <= this.width)
         {
            return;
         }
         SetTextOnly("");
         this.width = param1;
         m_nWidth = width + this.getRect(this).x * 2;
         m_bLabelTooWide = false;
         SetLabel(m_sText);
      }
      
      public function ResizingComplete() : void
      {
         m_bResized = true;
      }
   }
}

