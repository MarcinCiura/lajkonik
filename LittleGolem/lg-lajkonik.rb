#!/usr/bin/ruby -W0

require 'lg-interface'

class GTPClient
	def initialize(cmdline)
		@io=IO.popen(cmdline,'w+')
	end
	def cmd(c)
		@io.puts c.strip
		return @io.gets("\n\n")
	end
	def close
		@io.close
	end
end

class Castro < LittleGolemInterface
	include HavannahCoords
	LOGIN='Lajkonik_5_bot'
	PSW='hajijamy'
	BOSS_ID='MarcinCiura'
	def initialize
		@supported_gametypes = /Hav.*5/
		super(LOGIN,PSW,BOSS_ID)
	end

	def call_castro(size, moves)
		gtp = GTPClient.new("./lajkonik")
		gtp.cmd("boardsize #{size}")
		gtp.cmd("play_game " + moves.join(' '))
		color = ["white", "black"][moves.length % 2]
		response = gtp.cmd('genmove ' + color)
		gtp.cmd('quit')
		sleep 1
		gtp.close
		return response[2..-3] #trim off the = and newlines
	end
	def parse_make_moves(gameids)
		gameids.each do |g|
			if (game = get_game(g))
				size = game.scan(/SZ\[(.+?)\]/).flatten[0].to_i
				moves = game.scan(/;[B|W]\[(.+?)\]/).flatten.map{|m| coord_HGF2GA(m, size) }.compact

				self.log("Game #{g}, size #{size}: #{moves.join(' ')}")
				newmove = self.call_castro(size, moves)
				self.log("Game #{g}, size #{size}: response #{newmove}")
				self.post_move(g, coord_GA2LG(newmove, size))
			else
				self.log('error getting game')
				sleep(600)
			end
		end
	end
end

#enable to cause the http library to show warnings/errors
#$VERBOSE = 1

w=Castro.new
w.test_coords
loop {
	begin
		while w.parse
		end
		sleep(30)
	rescue Timeout::Error => e
		p 'timeout error (rbuff_fill exception), try again in 30 seconds'
		sleep(30)
	rescue => e
		p e.message
		p e.backtrace
		p 'An error... wait 5 minutes'
		sleep(300)
	end
}
