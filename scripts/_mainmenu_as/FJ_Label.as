package fourj.Base
{
   import flash.text.TextField;
   
   public class FJ_Label extends FJ_Base
   {
      
      private static const AVG_HEIGHT_LIMIT:Number = 5;
      
      public var m_iTextAlignment:int = 1;
      
      public var m_bInspectableMultiLine:Boolean = false;
      
      public var m_bInspectableWordWrap:Boolean = false;
      
      public var m_bInspectableUseEllipsis:Boolean = false;
      
      public var m_bInspectableVitaWordWrap:Boolean = false;
      
      public function FJ_Label()
      {
         super();
      }
      
      public function set bUseHtmlText(param1:Boolean) : void
      {
         m_bUseHtmlText = param1;
      }
      
      public function Init(param1:String) : void
      {
         SetLabelAlignment(m_iTextAlignment);
         InitBase(param1);
      }
      
      override public function SetLabel(param1:String) : void
      {
         SetLabelAlignment(m_iTextAlignment);
         m_bInitialised = true;
         m_bMultiLine = m_bInspectableMultiLine;
         m_bWordWrap = m_bInspectableWordWrap;
         m_bUseEllipsis = m_bInspectableUseEllipsis;
         m_bVitaWordWrap = m_bInspectableVitaWordWrap;
         super.SetLabel(param1);
      }
      
      public function GetAverageCharHeight() : int
      {
         var _loc1_:int = 0;
         var _loc2_:int = 0;
         var _loc3_:TextField = GetTextField();
         var _loc4_:int = 0;
         while(_loc4_ < _loc3_.text.length)
         {
            if(_loc3_.getCharBoundaries(_loc4_))
            {
               _loc2_ += _loc3_.getCharBoundaries(_loc4_).height;
               if(++_loc1_ >= AVG_HEIGHT_LIMIT)
               {
                  break;
               }
            }
            _loc4_++;
         }
         return int(_loc2_ / _loc1_);
      }
      
      override protected function GetLabelTooWide(param1:TextField) : void
      {
         var _loc2_:int = m_iWidestTextWidth > param1.textWidth ? m_iWidestTextWidth : int(param1.textWidth);
         var _loc3_:int = _loc2_ > width ? _loc2_ : int(width);
         m_iNewWidth = _loc3_ > m_iNewWidth ? _loc3_ : m_iNewWidth;
      }
   }
}

