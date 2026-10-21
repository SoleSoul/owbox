# ArchWiki page draft

`owbox.mediawiki` is a draft of an ArchWiki page about owbox, in the wiki's
markup: installing, starting, configuring and migrating from Openbox, and a
table of the programs that make a complete desktop with owbox. Keep it in
step with the code when the defaults or recommendations change.

GitHub can't show it properly: it doesn't know the wiki's templates
(`{{Pkg}}`, `{{ic}}`, `{{App}}`...). The wiki's own preview shows it as it
will look.

## Publishing it

1. Log in to the ArchWiki, open `User:<your name>/owbox`, paste the draft
   and use "Show preview". Save it there to have it reviewed or linked
   before it is an article.
2. Move it (the "Move" action) to `owbox`, or paste it into a new page
   `owbox`. `{{Lowercase title}}` keeps the title lowercase.
3. Link it from the list of stacking compositors in
   [Wayland#Stacking](https://wiki.archlinux.org/title/Wayland#Stacking),
   in alphabetical order (after Mutter, before waybox):

       * {{App|[[owbox]]|wlroots-based compositor built from Openbox's code, which uses Openbox's configuration files and themes.|https://github.com/SoleSoul/owbox|{{AUR|owbox}}}}

   Give the edit a summary such as "add owbox".
