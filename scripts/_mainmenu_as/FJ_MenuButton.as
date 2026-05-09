package fourj.Buttons
{
   import fourj.Base.FJ_Base;
   import fourj.Base.FJ_Button;
   
   public class FJ_MenuButton extends FJ_Button
   {
      
      public function FJ_MenuButton()
      {
         super();
      }
      
      override public function Init(param1:String, param2:int) : void
      {
         SetLabelAlignment(FJ_Base.ALIGN_CENTER);
         super.Init(param1,param2);
      }
   }
}

