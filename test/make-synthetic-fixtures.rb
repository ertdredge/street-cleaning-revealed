#!/usr/bin/env ruby
# Writes data/synthetic/*.json: variants of the real NWS capture
# (data/BOX-70,102-10-28-2026-full.txt) with a few values edited. Everything else in each file,
# structure, key order, intervals and the ~60 unused properties, is the real response, so the
# parser is exercised against realistic input. Used by weather-forecast-test.cpp, which holds the
# expected results. Run `make fixtures` after changing this file.
#
# The capture's data starts 2026-10-28T11:00Z (7 a.m. EDT) and runs about a week. Local time is
# US Eastern: EDT (UTC-4) until Sunday 2026-11-01 06:00Z, EST (UTC-5) after that.
require 'json'
require 'fileutils'
require 'time'

SRC = File.join(__dir__, 'data', 'historical', 'BOX-70,102-10-28-2026-full.json')
OUT = File.join(__dir__, 'data', 'synthetic')
FileUtils.mkdir_p(OUT)

def raw = JSON.parse(File.read(SRC))

def duration(s)
  m = s.match(/\AP(?:(\d+)D)?(?:T(?:(\d+)H)?(?:(\d+)M)?)?\z/) or raise "duration #{s}"
  m[1].to_i * 86_400 + m[2].to_i * 3600 + m[3].to_i * 60
end

def span(entry)
  s, d = entry['validTime'].split('/')
  t = Time.parse(s)
  [t, t + duration(d)]
end

def series(doc, name) = doc['properties'][name]['values']

# Spread `mm_total` evenly over the hourly snowfall entries that start in [from, to).
def set_snow(doc, from, to, mm_total)
  hours = series(doc, 'snowfallAmount').select { |e| s, = span(e); s >= from && s < to }
  raise "no hours in #{from}..#{to}" if hours.empty?
  hours.each { |e| e['value'] = mm_total.to_f / hours.size }
end

# Set the precipitation probability of every interval overlapping [from, to).
def set_pop(doc, from, to, pct)
  series(doc, 'probabilityOfPrecipitation').each do |e|
    s, en = span(e)
    e['value'] = pct if s < to && en > from
  end
end

def write(name, doc)
  body = doc.is_a?(String) ? doc : JSON.pretty_generate(doc, indent: '    ')
  File.write(File.join(OUT, name), body)
  puts "wrote #{name} (#{body.bytesize} bytes)"
end

def utc(*a) = Time.utc(*a)

# 6.0 inches (152.4 mm) tomorrow (Thu Oct 29) from 10:00 to 16:00 local, probability 90
d = raw
set_snow(d, utc(2026, 10, 29, 14), utc(2026, 10, 29, 20), 152.4)
set_pop(d, utc(2026, 10, 29, 14), utc(2026, 10, 29, 20), 90)
write('snow-tomorrow-6in.json', d)

# exactly 4.0 inches (101.6 mm) tomorrow from 14:00 to 18:00 local, probability 85
d = raw
set_snow(d, utc(2026, 10, 29, 18), utc(2026, 10, 29, 22), 101.6)
set_pop(d, utc(2026, 10, 29, 18), utc(2026, 10, 29, 22), 85)
write('snow-boundary-4in.json', d)

# a storm across midnight: 10 mm/hour from 22:00 Wed to 04:00 Thu local, probability 70
d = raw
set_snow(d, utc(2026, 10, 29, 2), utc(2026, 10, 29, 8), 60.0)
set_pop(d, utc(2026, 10, 29, 2), utc(2026, 10, 29, 8), 70)
write('snow-midnight-split.json', d)

# trace only: 0.1 mm/hour for eight hours, below the 0.25 mm/hour threshold
d = raw
set_snow(d, utc(2026, 10, 29, 12), utc(2026, 10, 29, 20), 0.8)
write('snow-trace-only.json', d)

# the same forecast with the highs in deg F
d = raw
mt = d['properties']['maxTemperature']
mt['uom'] = 'wmoUnit:degF'
mt['values'].each { |e| e['value'] = e['value'] * 9.0 / 5.0 + 32.0 }
write('highs-fahrenheit.json', d)

# an unrecognised snow unit
d = raw
d['properties']['snowfallAmount']['uom'] = 'wmoUnit:furlong'
write('snow-bad-unit.json', d)

# today's (Oct 28) high is gone, as it is late in the day
d = raw
series(d, 'maxTemperature').shift
write('no-high-today.json', d)

# snow around the fall-back change (Sun Nov 1, 02:00 EDT -> 01:00 EST), 5 mm in each hour:
#   Nov  1 03:00Z = 23:00 EDT Sat  -> Saturday
#   Nov  2 04:00Z = 23:00 EST Sun  -> Sunday (last hour of its 25)
#   Nov  2 05:00Z = 00:00 EST Mon  -> Monday
d = raw
[utc(2026, 11, 1, 3), utc(2026, 11, 2, 4), utc(2026, 11, 2, 5)].each { |t| set_snow(d, t, t + 3600, 5.0) }
write('snow-dst-fall-back.json', d)

# failure inputs
write('truncated.json', File.read(SRC).byteslice(0, 150_000))
write('error-page.html', "<html><body><h1>503 Service Unavailable</h1></body></html>\n")
write('problem.json', JSON.pretty_generate({ 'title' => 'Unexpected Problem', 'status' => 500 }))
write('empty.json', '')
