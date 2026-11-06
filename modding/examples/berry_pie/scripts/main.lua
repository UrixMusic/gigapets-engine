local G = Game.hooks()
function G:onLoad(ctx)
  if player.count("berry_pie:item/pie") == 0 then
    player.give("berry_pie:item/pie", 1)
    say("berry_pie:str/welcome")
  end
end
local S = System.hooks("care.decay")
function S:before(ctx)
  if ctx.stat == "hunger" then ctx.amount = ctx.amount * 0.5 end
end
